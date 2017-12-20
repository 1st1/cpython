#include <stdbool.h>
#include "Python.h"
#include "internal/pystate.h"


/*
Implementation is directly inspired by:
https://github.com/clojure/clojure/blob/master/src/jvm/clojure/lang/PersistentHashMap.java

PoC-quality implementation; do not use.
*/


#define IS_ARRAY_NODE(node)     (Py_TYPE(node) == &_PyHamt_ArrayNode_Type)
#define IS_BITMAP_NODE(node)    (Py_TYPE(node) == &_PyHamt_BitmapNode_Type)
#define IS_COLLISION_NODE(node) (Py_TYPE(node) == &_PyHamt_CollisionNode_Type)


typedef enum {ERROR, NOT_FOUND, FOUND} hamt_find_t;


#ifndef PyHamtNode_Bitmap_MAXSAVESIZE
#define PyHamtNode_Bitmap_MAXSAVESIZE 20
#endif
#ifndef PyHamtNode_Bitmap_MAXFREELIST
#define PyHamtNode_Bitmap_MAXFREELIST 5
#endif


#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
static PyHamtNode_Bitmap *free_bitmap_list[PyHamtNode_Bitmap_MAXSAVESIZE];
static int numfree_bitmap[PyHamtNode_Bitmap_MAXSAVESIZE];
#endif


#ifndef PyHamtNode_Array_MAXFREELIST
#define PyHamtNode_Array_MAXFREELIST 20
#endif


#if PyHamtNode_Array_MAXFREELIST > 0
static PyHamtNode_Array *free_array_list;
static int numfree_array;
#endif


static PyHamtNode_Bitmap *_empty_bitmap_node;


static PyHamtObject * _hamt_new(void);

static _PyHamtNode_BaseNode * hamt_node_array_new(Py_ssize_t);

static _PyHamtNode_BaseNode *
hamt_node_assoc(_PyHamtNode_BaseNode *node,
                uint32_t shift, int32_t hash,
                PyObject *key, PyObject *val, bool* added_leaf);

static hamt_find_t
hamt_node_find(_PyHamtNode_BaseNode *node,
               uint32_t shift, int32_t hash,
               PyObject *key, PyObject **val);

static _PyHamtNode_BaseNode *
hamt_node_create_twokeys_node(uint32_t shift,
                              PyObject *key1, PyObject *val1,
                              int32_t key2_hash,
                              PyObject *key2, PyObject *val2);

#ifdef Py_DEBUG
static int
hamt_node_dump(_PyHamtNode_BaseNode *node,
               _PyUnicodeWriter *writer, int level);
#endif


#ifdef Py_DEBUG
#define VALIDATE_ARRAY_NODE(NODE)                               \
    do {                                                        \
        assert(IS_ARRAY_NODE(NODE));                            \
        PyHamtNode_Array *node = (PyHamtNode_Array*)(NODE);     \
        Py_ssize_t i = 0, count = 0;                            \
        for (; i < _PyHamtNode_Array_size; i++) {               \
            if (node->a_array[i] != NULL) {                     \
                count++;                                        \
            }                                                   \
        }                                                       \
        assert(count == node->a_count);                         \
    } while (0);
#else
#define VALIDATE_ARRAY_NODE(node)
#endif



/* Returns -1 on error */
static inline int32_t
hamt_hash(PyObject *o)
{
    Py_hash_t hash = PyObject_Hash(o);

#if SIZEOF_PY_HASH_T <= 4
    return hash;
#else
    if (hash == -1) {
        /* exception */
        return -1;
    }

    /* While it's suboptimal to reduce Python's 64 bit hash to
       32 bits via xor, it seems that the resulting hash function
       is good enough.  This is also how Java hashes its Long type.
       Storing 10, 100, 1000 Python strings results in a relatively
       shallow uniform trie. */
    return (int32_t)(hash & 0xffffffffl) ^ (int32_t)(hash >> 32);
#endif
}


static inline uint32_t
hamt_mask(int32_t hash, uint32_t shift)
{
    return (((uint32_t)hash >> shift) & 0x01f);
}


static inline uint32_t
hamt_bitpos(int32_t hash, uint32_t shift)
{
    return (uint32_t)1 << hamt_mask(hash, shift);
}


static inline uint32_t
hamt_bitcount(uint32_t i)
{
#if defined(__GNUC__) && (__GNUC__ > 4)
    return (uint32_t)__builtin_popcountl(i);
#elif defined(__clang__) && (__clang_major__ > 3)
    return (uint32_t)__builtin_popcountl(i);
#else
    /* https://graphics.stanford.edu/~seander/bithacks.html */
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return ((i + (i >> 4) & 0xF0F0F0F) * 0x1010101) >> 24;
#endif
}


static inline uint32_t
hamt_bitindex(uint32_t bitmap, uint32_t bit)
{
    return hamt_bitcount(bitmap & (bit - 1));
}


/////////////////////////////////// Dump Helpers
#ifdef Py_DEBUG


static int
_hamt_dump_ident(_PyUnicodeWriter *writer, int level)
{
    /* Write `'    ' * level` to the `writer` */
    PyObject *str = NULL;
    PyObject *num = NULL;
    PyObject *res = NULL;
    int ret = -1;

    str = PyUnicode_FromString("    ");
    if (str == NULL) {
        goto error;
    }

    num = PyLong_FromLong((long)level);
    if (num == NULL) {
        goto error;
    }

    res = PyNumber_Multiply(str, num);
    if (res == NULL) {
        goto error;
    }

    ret = _PyUnicodeWriter_WriteStr(writer, res);

error:
    Py_XDECREF(res);
    Py_XDECREF(str);
    Py_XDECREF(num);
    return ret;
}


static int
_hamt_dump_format(_PyUnicodeWriter *writer, const char *format, ...)
{
    /* A convenient helper combining _PyUnicodeWriter_WriteStr and
       PyUnicode_FromFormatV.
    */
    PyObject* msg;
    int ret;

    va_list vargs;
#ifdef HAVE_STDARG_PROTOTYPES
    va_start(vargs, format);
#else
    va_start(vargs);
#endif
    msg = PyUnicode_FromFormatV(format, vargs);
    va_end(vargs);

    if (msg == NULL) {
        return -1;
    }

    ret = _PyUnicodeWriter_WriteStr(writer, msg);
    Py_DECREF(msg);
    return ret;
}


#endif  /* Py_DEBUG */
/////////////////////////////////// Bitmap Node


static _PyHamtNode_BaseNode *
hamt_node_bitmap_new(Py_ssize_t size)
{
    /* Create a new bitmap node of size 'size' */

    PyHamtNode_Bitmap *node;
    Py_ssize_t i;

    assert(size >= 0);
    assert(size % 2 == 0);

    if (size == 0 && _empty_bitmap_node != NULL) {
        Py_INCREF(_empty_bitmap_node);
        return (_PyHamtNode_BaseNode *)_empty_bitmap_node;
    }

#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
    /* We have a freelist for bitmap nodes.  Check if we are
       freelisting nodes of this size, and if there's a node
       we can reuse in the freelist.
    */
    if (size > 0 &&
            size < PyHamtNode_Bitmap_MAXSAVESIZE &&
            (node = free_bitmap_list[size]) != NULL)
    {
        free_bitmap_list[size] = (PyHamtNode_Bitmap *)node->b_array[0];
        numfree_bitmap[size]--;
        _Py_NewReference((PyObject *)node);
    }
    else
#endif
    {
        /* No freelist; allocate a new bitmap node */
        node = PyObject_GC_NewVar(
            PyHamtNode_Bitmap, &_PyHamt_BitmapNode_Type, size);
        if (node == NULL) {
            return NULL;
        }

        Py_SIZE(node) = size;
    }

    for (i = 0; i < size; i++) {
        node->b_array[i] = NULL;
    }

    node->b_bitmap = 0;

    _PyObject_GC_TRACK(node);

    if (size == 0 && _empty_bitmap_node == NULL) {
        /* Since bitmap nodes are immutable, we can cache the instance
           for size=0 and reuse it whenever we need an empty bitmap node.
        */
        _empty_bitmap_node = node;
        Py_INCREF(_empty_bitmap_node);
    }

    return (_PyHamtNode_BaseNode *)node;
}


static PyHamtNode_Bitmap *
hamt_node_bitmap_clone(PyHamtNode_Bitmap *o)
{
    /* Clone a bitmap node; return a new one with the same child notes. */

    PyHamtNode_Bitmap *clone;
    Py_ssize_t i;

    clone = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(Py_SIZE(o));
    if (clone == NULL) {
        return NULL;
    }

    for (i = 0; i < Py_SIZE(o); i++) {
        Py_XINCREF(o->b_array[i]);
        clone->b_array[i] = o->b_array[i];
    }

    clone->b_bitmap = o->b_bitmap;
    return clone;
}


static _PyHamtNode_BaseNode *
hamt_node_bitmap_assoc(PyHamtNode_Bitmap *self,
                       uint32_t shift, int32_t hash,
                       PyObject *key, PyObject *val, bool* added_leaf)
{
    /* assoc operation for bitmap nodes.

       Return: a new node, or self if key/val already is in the
       collection.

       'added_leaf' is later used in 'hamt_assoc' to determine if
       `hamt.set(key, val)` increased the size of the collection.
    */

    uint32_t bit = hamt_bitpos(hash, shift);
    uint32_t idx = hamt_bitindex(self->b_bitmap, bit);

    /* Bitmap node layout:

    +------+------+------+------+  ---  +------+------+
    | key1 | val1 | key2 | val2 |  ...  | keyN | valN |
    +------+------+------+------+  ---  +------+------+
    where `N < Py_SIZE(node)`.

    The `node->b_bitmap` field is a bitmap.  For a given
    `(shift, hash)` pair we can determine:

     - If this node has the corresponding key/val slots.
     - The index of key/val slots.
    */

    if ((self->b_bitmap & bit) != 0) {
        /* The key is set in this node */

        uint32_t key_idx = 2 * idx;
        uint32_t val_idx = key_idx + 1;

        assert(val_idx < Py_SIZE(self));

        PyObject *key_or_null = self->b_array[key_idx];
        PyObject *val_or_node = self->b_array[val_idx];

        if (key_or_null == NULL) {
            /* key is NULL.  This means that we have a few keys
               that have the same (hash, shift) pair. */

            assert(val_or_node != NULL);

            _PyHamtNode_BaseNode *sub_node = hamt_node_assoc(
                (_PyHamtNode_BaseNode *)val_or_node,
                shift + 5, hash, key, val, added_leaf);
            if (sub_node == NULL) {
                return NULL;
            }

            if (val_or_node == (PyObject *)sub_node) {
                Py_DECREF(sub_node);
                Py_INCREF(self);
                return (_PyHamtNode_BaseNode *)self;
            }

            PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
            if (ret == NULL) {
                return NULL;
            }
            Py_SETREF(ret->b_array[val_idx], (PyObject*)sub_node);
            return (_PyHamtNode_BaseNode *)ret;
        }

        assert(key != NULL);
        /* key is not NULL.  This means that we have only one other
           key in this collection that matches our hash for this shift. */

        int comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
        if (comp_err < 0) {  /* exception in __eq__ */
            return NULL;
        }
        if (comp_err == 1) {  /* key == key_or_null */
            comp_err = PyObject_RichCompareBool(val, val_or_node, Py_EQ);
            if (comp_err < 0) {
                return NULL;
            }
            if (comp_err == 1) {
                /* val == val_or_null: we already have the same key/val
                   pair; return self. */
                Py_INCREF(self);
                return (_PyHamtNode_BaseNode *)self;
            }

            /* We're setting a new value for the key we had before.
               Make a new bitmap node with a replaced value, and return it. */
            PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
            if (ret == NULL) {
                return NULL;
            }
            Py_INCREF(val);
            Py_SETREF(ret->b_array[val_idx], val);
            return (_PyHamtNode_BaseNode *)ret;
        }

        /* It's a new key, and it has the same index as *one* another key.
           We have a collision.  We need to create a new node which will
           combine the existing key and the key we're adding.

           `hamt_node_create_twokeys_node` will either create a new
           Collision node if the keys have identical hashes, or
           a new Bitmap node.
        */
        _PyHamtNode_BaseNode *sub_node = hamt_node_create_twokeys_node(
            shift + 5,
            key_or_null, val_or_node,  /* existing key/val */
            hash,
            key, val  /* new key/val */
        );
        if (sub_node == NULL) {
            return NULL;
        }

        PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
        if (ret == NULL) {
            Py_DECREF(sub_node);
            return NULL;
        }
        Py_SETREF(ret->b_array[key_idx], NULL);
        Py_SETREF(ret->b_array[val_idx], (PyObject *)sub_node);

        *added_leaf = true;
        return (_PyHamtNode_BaseNode *)ret;
    }
    else {
        /* There was no key before with the same (shift,hash). */

        uint32_t n = hamt_bitcount(self->b_bitmap);

        if (n >= 16) {
            /* When we have a situation where we want to store more
               than 16 nodes at one level of the tree, we no longer
               want to use the Bitmap node with bitmap encoding.

               Instead we start using an Array node, which has
               simpler (faster) implementation at the expense of
               having prealocated 32 pointers for its keys/values
               pairs.

               Small hamt objects (<30 keys) usually don't have any
               Array nodes at all.  Betwen ~30 and ~400 keys hamt
               objects usually have one Array node, and usually it's
               a root node.
            */

            uint32_t jdx = hamt_mask(hash, shift);
            /* 'jdx' is the index of where the new key should be added
               in the new Array node we're about to create. */

            _PyHamtNode_BaseNode *empty = NULL;
            PyHamtNode_Array *new_node = NULL;
            _PyHamtNode_BaseNode *res = NULL;

            /* Create a new Array node. */
            new_node = (PyHamtNode_Array *)hamt_node_array_new(n + 1);
            if (new_node == NULL) {
                goto fin;
            }

            /* Create an empty bitmap node for the next
               hamt_node_assoc call. */
            empty = hamt_node_bitmap_new(0);
            if (empty == NULL) {
                goto fin;
            }

            /* Make a new bitmap node for the key/val we're adding.
               Set that bitmap node to new-array-node[jdx]. */
            new_node->a_array[jdx] = hamt_node_assoc(
                empty, shift + 5, hash, key, val, added_leaf);
            if (new_node->a_array[jdx] == NULL) {
                goto fin;
            }

            /* Copy existing key/value pairs from the current Bitmap
               node to the new Array node we've just created. */
            Py_ssize_t i, j;
            for (i = 0, j = 0; i < _PyHamtNode_Array_size; i++) {
                if (((self->b_bitmap >> i) & 1) != 0) {
                    /* Ensure we don't accidentally override `jdx` element
                       we set few lines above.
                    */
                    assert(new_node->a_array[i] == NULL);

                    if (self->b_array[j] == NULL) {
                        new_node->a_array[i] =
                            (_PyHamtNode_BaseNode *)self->b_array[j + 1];
                        Py_INCREF(new_node->a_array[i]);
                    }
                    else {
                        new_node->a_array[i] = hamt_node_assoc(
                            empty, shift + 5,
                            hamt_hash(self->b_array[j]),
                            self->b_array[j],
                            self->b_array[j + 1],
                            added_leaf);

                        if (new_node->a_array[i] == NULL) {
                            goto fin;
                        }
                    }
                    j += 2;
                }
            }

            VALIDATE_ARRAY_NODE(new_node)

            /* That's it! */
            res = (_PyHamtNode_BaseNode *)new_node;

        fin:
            Py_XDECREF(empty);
            if (res == NULL) {
                Py_XDECREF(new_node);
            }
            return res;
        }
        else {
            /* We have less than 16 keys at this level; let's just
               create a new bitmap node out of this node with the
               new key/val pair added. */

            uint32_t key_idx = 2 * idx;
            uint32_t val_idx = key_idx + 1;
            Py_ssize_t i;

            *added_leaf = true;

            /* Allocate new Bitmap node which can have one more key/val
               pair in addition to what we have already. */
            PyHamtNode_Bitmap *new_node =
                (PyHamtNode_Bitmap *)hamt_node_bitmap_new(2 * (n + 1));
            if (new_node == NULL) {
                return NULL;
            }

            /* Copy all keys/values that will be before the new key/value
               we are adding. */
            for (i = 0; i < key_idx; i++) {
                Py_XINCREF(self->b_array[i]);
                new_node->b_array[i] = self->b_array[i];
            }

            /* Set the new key/value to the new Bitmap node. */
            Py_INCREF(key);
            new_node->b_array[key_idx] = key;
            Py_INCREF(val);
            new_node->b_array[val_idx] = val;

            /* Copy all keys/values that will be after the new key/value
               we are adding. */
            for (i = key_idx; i < Py_SIZE(self); i++) {
                Py_XINCREF(self->b_array[i]);
                new_node->b_array[i + 2] = self->b_array[i];
            }

            new_node->b_bitmap = self->b_bitmap | bit;
            return (_PyHamtNode_BaseNode *)new_node;
        }
    }
}


static hamt_find_t
hamt_node_bitmap_find(PyHamtNode_Bitmap *self,
                      uint32_t shift, int32_t hash,
                      PyObject *key, PyObject **val)
{
    /* Lookup a key in a Bitmap node. */

    uint32_t bit = hamt_bitpos(hash, shift);
    uint32_t idx;
    uint32_t key_idx;
    uint32_t val_idx;
    PyObject *key_or_null;
    PyObject *val_or_node;
    int comp_err;

    if ((self->b_bitmap & bit) == 0) {
        return NOT_FOUND;
    }

    idx = hamt_bitindex(self->b_bitmap, bit);
    assert(idx >= 0);
    key_idx = idx * 2;
    val_idx = key_idx + 1;

    assert(val_idx < Py_SIZE(self));

    key_or_null = self->b_array[key_idx];
    val_or_node = self->b_array[val_idx];

    if (key_or_null == NULL) {
        /* There are a few keys that have the same hash at the current shift
           that match our key.  Dispatch the lookup further down the tree. */
        assert(val_or_node != NULL);
        return hamt_node_find((_PyHamtNode_BaseNode *)val_or_node,
                              shift + 5, hash, key, val);
    }

    /* We have only one key -- a potential match.  Let's compare if the
       key we are looking at is equal to the key we are looking for. */
    assert(key != NULL);
    comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
    if (comp_err < 0) {  /* exception in __eq__ */
        return ERROR;
    }
    if (comp_err == 1) {  /* key == key_or_null */
        *val = val_or_node;
        return FOUND;
    }

    return NOT_FOUND;
}


static int
hamt_node_bitmap_traverse(PyHamtNode_Bitmap *self, visitproc visit, void *arg)
{
    /* Bitmap's tp_traverse */

    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->b_array[i]);
    }

    return 0;
}


static void
hamt_node_bitmap_dealloc(PyHamtNode_Bitmap *self)
{
    /* Bitmap's tp_dealloc */

    Py_ssize_t len = Py_SIZE(self);
    Py_ssize_t i;

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    if (len > 0) {
        i = len;
        while (--i >= 0) {
            Py_XDECREF(self->b_array[i]);
        }

#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
        /* Check if we can add this node to the freelist of Bitmap nodes. */
        if (len < PyHamtNode_Bitmap_MAXSAVESIZE &&
                numfree_bitmap[len] < PyHamtNode_Bitmap_MAXFREELIST)
        {
            self->b_array[0] = (PyObject*) free_bitmap_list[len];
            free_bitmap_list[len] = self;
            numfree_bitmap[len]++;
            goto done;
        }

#endif
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
done:
    Py_TRASHCAN_SAFE_END(self)
}


#ifdef Py_DEBUG
static int
hamt_node_bitmap_dump(PyHamtNode_Bitmap *node,
                      _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Bitmap nodes. */

    Py_ssize_t i;
    PyObject *tmp1;
    PyObject *tmp2;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "BitmapNode(size=%zd ",
                          Py_SIZE(node)))
    {
        goto error;
    }

    tmp1 = PyLong_FromUnsignedLong(node->b_bitmap);
    if (tmp1 == NULL) {
        goto error;
    }
    tmp2 = _PyLong_Format(tmp1, 2);
    Py_DECREF(tmp1);
    if (tmp2 == NULL) {
        goto error;
    }
    if (_hamt_dump_format(writer, "bitmap=%S id=%p):\n", tmp2, node)) {
        Py_DECREF(tmp2);
        goto error;
    }
    Py_DECREF(tmp2);

    for (i = 0; i < Py_SIZE(node); i += 2) {
        PyObject *key_or_null = node->b_array[i];
        PyObject *val_or_node = node->b_array[i + 1];

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (key_or_null == NULL) {
            if (_hamt_dump_format(writer, "NULL:\n")) {
                goto error;
            }

            if (hamt_node_dump((_PyHamtNode_BaseNode *)val_or_node,
                               writer, level + 2))
            {
                goto error;
            }
        }
        else {
            if (_hamt_dump_format(writer, "%R: %R", key_or_null,
                                  val_or_node))
            {
                goto error;
            }
        }

        if (_hamt_dump_format(writer, "\n")) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Collision Node


static _PyHamtNode_BaseNode *
hamt_node_collision_new(int32_t hash, Py_ssize_t size)
{
    /* Create a new Collision node. */

    PyHamtNode_Collision *node;
    Py_ssize_t i;

    assert(size >= 0);
    assert(size % 2 == 0);

    node = PyObject_GC_NewVar(
        PyHamtNode_Collision, &_PyHamt_CollisionNode_Type, size);
    if (node == NULL) {
        return NULL;
    }

    for (i = 0; i < size; i++) {
        node->c_array[i] = NULL;
    }

    Py_SIZE(node) = size;
    node->c_hash = hash;

    _PyObject_GC_TRACK(node);

    return (_PyHamtNode_BaseNode *)node;
}


static hamt_find_t
hamt_node_collision_find_index(PyHamtNode_Collision *self, PyObject *key,
                               Py_ssize_t *idx)
{
    /* Lookup `key` in the Collision node `self`.  Set the index of the
       found key to 'idx'. */

    Py_ssize_t i;
    PyObject *el;

    for (i = 0; i < Py_SIZE(self); i += 2) {
        el = self->c_array[i];

        assert(el != NULL);
        int cmp = PyObject_RichCompareBool(key, el, Py_EQ);
        if (cmp < 0) {
            return ERROR;
        }
        if (cmp == 1) {
            *idx = i;
            return FOUND;
        }
    }

    return NOT_FOUND;
}


static _PyHamtNode_BaseNode *
hamt_node_collision_assoc(PyHamtNode_Collision *self,
                          uint32_t shift, int32_t hash,
                          PyObject *key, PyObject *val, bool* added_leaf)
{
    /* Set a new key to this level (currently a Collision node)
       of the tree. */

    if (hash == self->c_hash) {
        /* The hash of the 'key' we are adding matches the hash of
           other keys in this Collision node. */

        Py_ssize_t key_idx = -1;
        hamt_find_t found;
        PyHamtNode_Collision *new_node;
        Py_ssize_t i;

        /* Let's try to lookup the new 'key', maybe we already have it. */
        found = hamt_node_collision_find_index(self, key, &key_idx);
        switch (found) {
            case ERROR:
                /* Exception. */
                return NULL;

            case NOT_FOUND:
                /* This is a totally new key.  Clone the current node,
                   add a new key/value to the cloned node. */

                new_node = (PyHamtNode_Collision *)hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self) + 2);
                if (new_node == NULL) {
                    return NULL;
                }

                for (i = 0; i < Py_SIZE(self); i++) {
                    Py_INCREF(self->c_array[i]);
                    new_node->c_array[i] = self->c_array[i];
                }

                Py_INCREF(key);
                new_node->c_array[i] = key;
                Py_INCREF(val);
                new_node->c_array[i + 1] = val;

                *added_leaf = true;
                return (_PyHamtNode_BaseNode *)new_node;

            case FOUND:
                /* There's a key which is equal to the key we are adding. */

                assert(key_idx >= 0);
                assert(key_idx < Py_SIZE(self));
                Py_ssize_t val_idx = key_idx + 1;

                /* Check if the existing value for the key is equal
                   to the value that we're setting. */
                int cmp = PyObject_RichCompareBool(
                    self->c_array[val_idx], val, Py_EQ);
                if (cmp < 0) {
                    /* Exception */
                    return NULL;
                }
                if (cmp == 1) {
                    /* We're setting a key/value pair that's already set. */
                    Py_INCREF(self);
                    return (_PyHamtNode_BaseNode *)self;
                }

                /* We need to replace old value for the key
                   with a new value.  Create a new Collision node.*/
                new_node = (PyHamtNode_Collision *)hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self));
                if (new_node == NULL) {
                    return NULL;
                }

                /* Copy all elements of the old node to the new one. */
                for (i = 0; i < Py_SIZE(self); i++) {
                    Py_INCREF(self->c_array[i]);
                    new_node->c_array[i] = self->c_array[i];
                }

                /* Replace the old value with the new value for the our key. */
                Py_DECREF(new_node->c_array[val_idx]);
                Py_INCREF(val);
                new_node->c_array[val_idx] = val;

                return (_PyHamtNode_BaseNode *)new_node;
        }
    }
    else {
        /* The hash of the new key is different from the hash that
           all keys of this Collision node have.

           Create a Bitmap node inplace with two children:
           key/value pair that we're adding, and the Collision node
           we're replacing on this tree level.
        */

        PyHamtNode_Bitmap *new_node;
        _PyHamtNode_BaseNode *assoc_res;

        new_node = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(2);
        if (new_node == NULL) {
            return NULL;
        }
        new_node->b_bitmap = hamt_bitpos(self->c_hash, shift);
        Py_INCREF(self);
        new_node->b_array[1] = (PyObject*) self;

        assoc_res = hamt_node_bitmap_assoc(
            new_node, shift, hash, key, val, added_leaf);
        Py_DECREF(new_node);
        return assoc_res;
    }
}


static hamt_find_t
hamt_node_collision_find(PyHamtNode_Collision *self,
                         uint32_t shift, int32_t hash,
                         PyObject *key, PyObject **val)
{
    /* Lookup `key` in the Collision node `self`.  Set the value
       for the found key to 'val'. */

    Py_ssize_t idx = -1;
    hamt_find_t res;

    res = hamt_node_collision_find_index(self, key, &idx);
    if (res == ERROR || res == NOT_FOUND) {
        return res;
    }

    assert(idx >= 0);
    assert(idx + 1 < Py_SIZE(self));

    *val = self->c_array[idx + 1];
    assert(*val != NULL);

    return FOUND;
}


static int
hamt_node_collision_traverse(PyHamtNode_Collision *self,
                             visitproc visit, void *arg)
{
    /* Collision's tp_traverse */

    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->c_array[i]);
    }

    return 0;
}


static void
hamt_node_collision_dealloc(PyHamtNode_Collision *self)
{
    /* Collision's tp_dealloc */

    Py_ssize_t len = Py_SIZE(self);

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    if (len > 0) {
        while (--len >= 0) {
            Py_XDECREF(self->c_array[len]);
        }
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
    Py_TRASHCAN_SAFE_END(self)
}


#ifdef Py_DEBUG
static int
hamt_node_collision_dump(PyHamtNode_Collision *node,
                         _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Collision nodes. */

    Py_ssize_t i;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "CollisionNode(size=%zd id=%p):\n",
                          Py_SIZE(node), node))
    {
        goto error;
    }

    for (i = 0; i < Py_SIZE(node); i += 2) {
        PyObject *key = node->c_array[i];
        PyObject *val = node->c_array[i + 1];

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "%R: %R\n", key, val)) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Array Node


static _PyHamtNode_BaseNode *
hamt_node_array_new(Py_ssize_t count)
{
    /* Create a new Array node. */

    PyHamtNode_Array *node;
    Py_ssize_t i;

#if PyHamtNode_Array_MAXFREELIST > 0
    /* Check if there's a node in the freelist. */
    if ((node = free_array_list) != NULL) {
        free_array_list = (PyHamtNode_Array *)node->a_array[0];
        numfree_array--;
        _Py_NewReference((PyObject *)node);
    }
    else
#endif
    {
        /* Allocate a new Array node. */
        node = PyObject_GC_New(PyHamtNode_Array, &_PyHamt_ArrayNode_Type);
        if (node == NULL) {
            return NULL;
        }
    }

    for (i = 0; i < _PyHamtNode_Array_size; i++) {
        node->a_array[i] = NULL;
    }

    node->a_count = count;

    _PyObject_GC_TRACK(node);
    return (_PyHamtNode_BaseNode *)node;
}


static _PyHamtNode_BaseNode *
hamt_node_array_assoc(PyHamtNode_Array *self,
                      uint32_t shift, int32_t hash,
                      PyObject *key, PyObject *val, bool* added_leaf)
{
    /* Set a new key to this level (currently a Collision node)
       of the tree.

       Array nodes don't store values, they can only point to
       other nodes.  They are simple arrays of 32 BaseNode pointers/
     */

    uint32_t idx = hamt_mask(hash, shift);
    _PyHamtNode_BaseNode *node = self->a_array[idx];
    _PyHamtNode_BaseNode *child_node;
    PyHamtNode_Array *new_node;
    Py_ssize_t i;

    if (node == NULL) {
        /* There's no child node for the given hash.  Create a new
           Bitmap node for this key. */

        PyHamtNode_Bitmap *empty = NULL;

        /* Get an empty Bitmap node to work with. */
        empty = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(0);
        if (empty == NULL) {
            return NULL;
        }

        /* Set key/val to the newly created empty Bitmap, thus
           creating a new Bitmap node with our key/value pair. */
        child_node = hamt_node_bitmap_assoc(
            empty,
            shift + 5, hash, key, val, added_leaf);
        Py_DECREF(empty);
        if (child_node == NULL) {
            return NULL;
        }

        /* Create a new Array node. */
        new_node = (PyHamtNode_Array *)hamt_node_array_new(self->a_count + 1);
        if (new_node == NULL) {
            Py_DECREF(child_node);
            return NULL;
        }

        /* Copy all elements from the current Array node to the
           new one. */
        for (i = 0; i < _PyHamtNode_Array_size; i++) {
            Py_XINCREF(self->a_array[i]);
            new_node->a_array[i] = self->a_array[i];
        }

        assert(new_node->a_array[idx] == NULL);
        new_node->a_array[idx] = child_node;  /* borrow */
        VALIDATE_ARRAY_NODE(new_node)
    }
    else {
        /* There's a child node for the given hash.
           Set the key to it./ */
        child_node = hamt_node_assoc(
            node, shift + 5, hash, key, val, added_leaf);
        if (child_node == (_PyHamtNode_BaseNode *)self) {
            Py_DECREF(child_node);
            return (_PyHamtNode_BaseNode *)self;
        }

        /* Create a new Array node. */
        new_node = (PyHamtNode_Array *)hamt_node_array_new(self->a_count);
        if (new_node == NULL) {
            Py_DECREF(child_node);
            return NULL;
        }


        /* Copy all elements from the current Array node to the
           new one. */
        for (i = 0; i < _PyHamtNode_Array_size; i++) {
            Py_XINCREF(self->a_array[i]);
            new_node->a_array[i] = self->a_array[i];
        }

        Py_DECREF(new_node->a_array[idx]);
        new_node->a_array[idx] = child_node;  /* borrow */
        VALIDATE_ARRAY_NODE(new_node)
    }

    return (_PyHamtNode_BaseNode *)new_node;
}


static hamt_find_t
hamt_node_array_find(PyHamtNode_Array *self,
                     uint32_t shift, int32_t hash,
                     PyObject *key, PyObject **val)
{
    /* Lookup `key` in the Array node `self`.  Set the value
       for the found key to 'val'. */

    uint32_t idx = hamt_mask(hash, shift);
    _PyHamtNode_BaseNode *node;

    node = self->a_array[idx];
    if (node == NULL) {
        return NOT_FOUND;
    }

    /* Dispatch to the generic hamt_node_find */
    return hamt_node_find(node, shift + 5, hash, key, val);
}


static int
hamt_node_array_traverse(PyHamtNode_Array *self,
                         visitproc visit, void *arg)
{
    /* Array's tp_traverse */

    Py_ssize_t i;

    for (i = 0; i < _PyHamtNode_Array_size; i++) {
        Py_VISIT(self->a_array[i]);
    }

    return 0;
}


static void
hamt_node_array_dealloc(PyHamtNode_Array *self)
{
    /* Array's tp_dealloc */

    Py_ssize_t i;

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    for (i = 0; i < _PyHamtNode_Array_size; i++) {
        Py_XDECREF(self->a_array[i]);
    }

#if PyHamtNode_Array_MAXFREELIST > 0
    if (numfree_array < PyHamtNode_Array_MAXFREELIST) {
        self->a_array[0] = (_PyHamtNode_BaseNode *)free_array_list;
        free_array_list = self;
        numfree_array++;
        goto done;
    }
#endif

    Py_TYPE(self)->tp_free((PyObject *)self);
done:
    Py_TRASHCAN_SAFE_END(self)
}


#ifdef Py_DEBUG
static int
hamt_node_array_dump(PyHamtNode_Array *node,
                     _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Array nodes. */

    Py_ssize_t i;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "ArrayNode(id=%p):\n", node)) {
        goto error;
    }

    for (i = 0; i < _PyHamtNode_Array_size; i++) {
        if (node->a_array[i] == NULL) {
            continue;
        }

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "%d::\n", i)) {
            goto error;
        }

        if (hamt_node_dump(node->a_array[i], writer, level + 1)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "\n")) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Node Dispatch


static _PyHamtNode_BaseNode *
hamt_node_assoc(_PyHamtNode_BaseNode *node,
                uint32_t shift, int32_t hash,
                PyObject *key, PyObject *val, bool* added_leaf)
{
    /* Set key/value to the 'node' starting with the given shift/hash.
       Return a new node, or the same node if key/value already
       set.

       added_leaf will be set to 1 if key/value wasn't in the
       tree before.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype}_assoc method.
    */

    if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_assoc(
            (PyHamtNode_Array *)node,
            shift, hash, key, val, added_leaf);
    }
    else if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_assoc(
            (PyHamtNode_Bitmap *)node,
            shift, hash, key, val, added_leaf);
    }
    else if (IS_COLLISION_NODE(node)) {
        return hamt_node_collision_assoc(
            (PyHamtNode_Collision *)node,
            shift, hash, key, val, added_leaf);
    }

    assert(0);
    return NULL;
}


static hamt_find_t
hamt_node_find(_PyHamtNode_BaseNode *node,
               uint32_t shift, int32_t hash,
               PyObject *key, PyObject **val)
{
    /* Find the key in the node starting with the given shift/hash.

       If a value is found, the result will be set to FOUND, and
       *val will point to the found value object.

       If a value wasn't found, the result will be set to NOT_FOUND.

       If an exception occurs during the call, the result will be ERROR.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype}_find method.
    */

    if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_find(
            (PyHamtNode_Array *)node,
            shift, hash, key, val);
    }
    else if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_find(
            (PyHamtNode_Bitmap *)node,
            shift, hash, key, val);

    }
    else if (IS_COLLISION_NODE(node)) {
        return hamt_node_collision_find(
            (PyHamtNode_Collision *)node,
            shift, hash, key, val);
    }

    assert(0);
    return ERROR;
}


#ifdef Py_DEBUG
static int
hamt_node_dump(_PyHamtNode_BaseNode *node,
               _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for a node.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype})_dump method.
    */

    if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_dump(
            (PyHamtNode_Array *)node, writer, level);
    }
    else if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_dump(
            (PyHamtNode_Bitmap *)node, writer, level);
    }
    else if (IS_COLLISION_NODE(node)) {
        return hamt_node_collision_dump(
            (PyHamtNode_Collision *)node, writer, level);
    }

    assert(0);
    return -1;
}
#endif  /* Py_DEBUG */


static _PyHamtNode_BaseNode *
hamt_node_create_twokeys_node(uint32_t shift,
                              PyObject *key1, PyObject *val1,
                              int32_t key2_hash,
                              PyObject *key2, PyObject *val2)
{
    /* Helper method.  Creates a new node for key1/val and key2/val2
       pairs.

       If key1 hash is equal to the hash of key2, a Collision node
       will be created.  If they are not equal, a Bitmap node is
       created.
    */

    bool added_leaf = false;
    _PyHamtNode_BaseNode *n;
    _PyHamtNode_BaseNode *n2;
    int32_t key1_hash;

    key1_hash = hamt_hash(key1);
    if (key1_hash == -1) {
        return NULL;
    }

    if (key1_hash == key2_hash) {
        PyHamtNode_Collision *n;
        n = (PyHamtNode_Collision *)hamt_node_collision_new(key1_hash, 4);
        if (n == NULL) {
            return NULL;
        }

        Py_INCREF(key1);
        n->c_array[0] = key1;
        Py_INCREF(val1);
        n->c_array[1] = val1;
        Py_INCREF(key2);
        n->c_array[2] = key2;
        Py_INCREF(val2);
        n->c_array[3] = val2;

        return (_PyHamtNode_BaseNode *)n;
    }

    n = hamt_node_bitmap_new(0);
    if (n == NULL) {
        return NULL;
    }

    n2 = hamt_node_bitmap_assoc((PyHamtNode_Bitmap *)n,
                                shift, key1_hash, key1, val1, &added_leaf);
    Py_DECREF(n);
    if (n2 == NULL) {
        return NULL;
    }

    n = hamt_node_assoc(n2, shift, key2_hash, key2, val2, &added_leaf);
    Py_DECREF(n2);
    if (n == NULL) {
        return NULL;
    }

    return n;
}


/////////////////////////////////// Hamt Object



static PyHamtObject *
hamt_assoc(PyHamtObject *o, PyObject *key, PyObject *val)
{
    int32_t key_hash;
    bool added_leaf = false;
    _PyHamtNode_BaseNode *new_root;
    PyHamtObject *new_o;

    key_hash = hamt_hash(key);
    if (key_hash == -1) {
        return NULL;
    }

    new_root = hamt_node_assoc(
        (_PyHamtNode_BaseNode *)(o->h_root),
        0, key_hash, key, val, &added_leaf);

    if (new_root == NULL) {
        return NULL;
    }

    if (new_root == o->h_root) {
        Py_DECREF(new_root);
        Py_INCREF(o);
        return o;
    }

    new_o = _hamt_new();
    if (new_o == NULL) {
        Py_DECREF(new_root);
        return NULL;
    }

    new_o->h_root = new_root;  /* borrow */
    new_o->h_count = added_leaf ? o->h_count + 1 : o->h_count;

    return new_o;
}


static hamt_find_t
hamt_find(PyHamtObject *o, PyObject *key, PyObject **val)
{
    int32_t key_hash;

    key_hash = hamt_hash(key);
    if (key_hash == -1) {
        return ERROR;
    }

    return hamt_node_find(o->h_root, 0, key_hash, key, val);
}


static int
hamt_clear(PyHamtObject *self)
{
    Py_CLEAR(self->h_root);
    return 0;
}


static int
hamt_traverse(PyHamtObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->h_root);
    return 0;
}


static PyHamtObject *
_hamt_new(void)
{
    PyHamtObject *o;
    o = PyObject_GC_New(PyHamtObject, &PyHamt_Type);
    if (o == NULL) {
        return NULL;
    }
    PyObject_GC_Track(o);
    return o;
}


static PyObject *
hamt_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyObject *self;
    PyHamtObject *h;

    assert(type != NULL && type->tp_alloc != NULL);
    self = type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    h = (PyHamtObject *)self;
    h->h_count = 0;

    h->h_root = hamt_node_bitmap_new(0);
    if (h->h_root == NULL) {
        return NULL;
    }

    return self;
}


static void
hamt_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    (void)hamt_clear((PyHamtObject*)self);
    Py_TYPE(self)->tp_free(self);
}


#ifdef Py_DEBUG
static PyObject *
hamt_dump(PyHamtObject *self)
{
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);

    if (_hamt_dump_format(&writer, "HAMT(len=%zd):\n", self->h_count)) {
        goto error;
    }

    if (hamt_node_dump(self->h_root, &writer, 0)) {
        goto error;
    }

    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Hamt Methods


static PyObject *
hamt_py_set(PyHamtObject *self, PyObject *args)
{
    PyObject *key;
    PyObject *val;

    if (!PyArg_UnpackTuple(args, "set", 2, 2, &key, &val)) {
        return NULL;
    }

    return (PyObject *)hamt_assoc(self, key, val);
}


static PyObject *
hamt_py_get(PyHamtObject *self, PyObject *args)
{
    PyObject *key;
    PyObject *def = NULL;
    PyObject *val = NULL;
    hamt_find_t res;

    if (!PyArg_UnpackTuple(args, "get", 1, 2, &key, &def))
    {
        return NULL;
    }

    res = hamt_find(self, key, &val);
    switch (res) {
        case ERROR:
            return NULL;
        case FOUND:
            Py_INCREF(val);
            return val;
        case NOT_FOUND:
            if (def == NULL) {
                Py_RETURN_NONE;
            }
            Py_INCREF(def);
            return def;
    }
}


#ifdef Py_DEBUG
static PyObject *
hamt_py_dump(PyHamtObject *self, PyObject *args)
{
    return hamt_dump(self);
}
#endif


static Py_ssize_t
hamt_py_len(PyHamtObject *self)
{
    return self->h_count;
}


static PyMethodDef PyHamt_methods[] = {
    {"set", (PyCFunction)hamt_py_set, METH_VARARGS, NULL},
    {"get", (PyCFunction)hamt_py_get, METH_VARARGS, NULL},
#ifdef Py_DEBUG
    {"__dump__", (PyCFunction)hamt_py_dump, METH_NOARGS, NULL},
#endif
    {NULL, NULL}
};


static PyMappingMethods PyHamt_as_mapping = {
    (lenfunc)hamt_py_len,
};


///////////////////////////////////


PyTypeObject PyHamt_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt",
    sizeof(PyHamtObject),
    .tp_methods = PyHamt_methods,
    .tp_as_mapping = &PyHamt_as_mapping,
    .tp_dealloc = (destructor)hamt_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_traverse,
    .tp_clear = (inquiry)hamt_clear,
    .tp_new = hamt_new,
};


PyTypeObject _PyHamt_ArrayNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_array_node",
    sizeof(PyHamtNode_Array),
    0,
    .tp_dealloc = (destructor)hamt_node_array_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_array_traverse,
    .tp_free = PyObject_GC_Del,
};


PyTypeObject _PyHamt_BitmapNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_bitmap_node",
    sizeof(PyHamtNode_Bitmap) - sizeof(PyObject *),
    sizeof(PyObject *),
    .tp_dealloc = (destructor)hamt_node_bitmap_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_bitmap_traverse,
    .tp_free = PyObject_GC_Del,
};


PyTypeObject _PyHamt_CollisionNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_collision_node",
    sizeof(PyHamtNode_Collision) - sizeof(PyObject *),
    sizeof(PyObject *),
    .tp_dealloc = (destructor)hamt_node_collision_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_collision_traverse,
    .tp_free = PyObject_GC_Del,
};
