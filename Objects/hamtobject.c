#include <stdbool.h>
#include "Python.h"


typedef enum {ERROR, NOT_FOUND, FOUND} hamt_find_t;


static PyHamtNode_Bitmap *_empty_bitmap_node;


static PyHamtObject *
_hamt_new(void);

static _PyHamtNode_BaseNode *
hamt_node_assoc(_PyHamtNode_BaseNode *node,
                int32_t shift, int32_t hash,
                PyObject *key, PyObject *val, bool* added_leaf);

static hamt_find_t
hamt_node_find(_PyHamtNode_BaseNode *node,
               int32_t shift, int32_t hash,
               PyObject *key, PyObject **val);

static _PyHamtNode_BaseNode *
hamt_node_create(int32_t shift, PyObject *key1, PyObject *val1,
                 int32_t key2_hash, PyObject *key2, PyObject *val2);

static int
hamt_node_dump(_PyHamtNode_BaseNode *node,
               _PyUnicodeWriter *writer, int level);


/* Returns -1 on error */
static inline int32_t
hamt_hash(PyObject *o)
{
    Py_hash_t hash = PyObject_Hash(o);

    if (hash == -1) {
        /* exception */
        return -1;
    }

    return (int32_t)(hash & 0xffffffffl) ^ (int32_t)(hash >> 32);
}


static inline int32_t
hamt_mask(int32_t hash, int32_t shift)
{
    return (int32_t)(((uint32_t)hash >> (uint32_t)shift) & 0x01f);
}


static inline int32_t
hamt_bitpos(int32_t hash, int32_t shift)
{
    return 1 << hamt_mask(hash, shift);
}


static inline int32_t
hamt_bitcount(int32_t i)
{
    return __builtin_popcount(i);
}


static inline int32_t
hamt_bitindex(int32_t bitmap, int32_t bit)
{
    return hamt_bitcount(bitmap & (bit - 1));
}


/////////////////////////////////// Dump Helpers


static int
_hamt_dump_ident(_PyUnicodeWriter *writer, int level)
{
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


/////////////////////////////////// Bitmap Node


static _PyHamtNode_BaseNode *
hamt_node_bitmap_new(Py_ssize_t size)
{
    PyHamtNode_Bitmap *node;
    Py_ssize_t i;

    assert(size >= 0);

    if (size == 0 && _empty_bitmap_node != NULL) {
        Py_INCREF(_empty_bitmap_node);
        return (_PyHamtNode_BaseNode *)_empty_bitmap_node;
    }

    node = PyObject_GC_NewVar(
        PyHamtNode_Bitmap, &_PyHamt_BitmapNode_Type, size);
    if (node == NULL) {
        return NULL;
    }

    for (i = 0; i < size; i++) {
        node->b_array[i] = NULL;
    }

    Py_SIZE(node) = size;
    node->b_type = Bitmap;
    node->b_bitmap = 0;

    _PyObject_GC_TRACK(node);

    if (size == 0 && _empty_bitmap_node == NULL) {
        _empty_bitmap_node = node;
        Py_INCREF(_empty_bitmap_node);
    }

    return (_PyHamtNode_BaseNode *)node;
}


static PyHamtNode_Bitmap *
hamt_node_bitmap_clone(PyHamtNode_Bitmap *o)
{
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


static PyHamtNode_Bitmap *
hamt_node_bitmap_clone_and_set(PyHamtNode_Bitmap *o,
                               int32_t idx, PyObject *val)
{
    PyHamtNode_Bitmap *new_node;

    new_node = hamt_node_bitmap_clone(o);
    if (new_node == NULL) {
        return NULL;
    }

    Py_DECREF(new_node->b_array[idx]);
    Py_XINCREF(val);
    new_node->b_array[idx] = val;
    return new_node;

}


static _PyHamtNode_BaseNode *
hamt_node_bitmap_assoc(PyHamtNode_Bitmap *self,
                       int32_t shift, int32_t hash,
                       PyObject *key, PyObject *val, bool* added_leaf)
{
    int32_t bit = hamt_bitpos(hash, shift);
    int32_t idx = hamt_bitindex(self->b_bitmap, bit);
    _PyHamtNode_BaseNode *ret;
    int comp_err;

    assert(idx >= 0);

    if ((self->b_bitmap & bit) != 0) {
        /* The key is set in this node */

        int32_t key_idx = 2 * idx;
        int32_t val_idx = key_idx + 1;
        _PyHamtNode_BaseNode *n;
        PyHamtNode_Bitmap *n2;

        assert(val_idx < Py_SIZE(self));

        PyObject *key_or_null = self->b_array[key_idx];
        PyObject *val_or_node = self->b_array[val_idx];

        if (key_or_null == NULL) {
            assert(val_or_node != NULL);

            n = hamt_node_assoc(
                (_PyHamtNode_BaseNode *)val_or_node,
                shift + 5, hash, key, val, added_leaf);

            if (n == NULL) {
                return NULL;
            }

            if (val_or_node == (PyObject *)n) {
                Py_DECREF(n);
                Py_INCREF(self);
                return (_PyHamtNode_BaseNode *)self;
            }

            ret = (_PyHamtNode_BaseNode *)hamt_node_bitmap_clone_and_set(
                self, val_idx, (PyObject *)n);
            Py_DECREF(n);
            return ret;
        }

        assert(key != NULL);
        comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
        if (comp_err < 0) {  /* exception in __eq__ */
            return NULL;
        }
        if (comp_err == 1) {  /* key == key_or_null */
            if (val == val_or_node) {
                Py_INCREF(self);
                return (_PyHamtNode_BaseNode *)self;
            }

            return (_PyHamtNode_BaseNode *)hamt_node_bitmap_clone_and_set(
                self, val_idx, val);
        }

        *added_leaf = true;
        n = hamt_node_create(shift + 5, key_or_null, val_or_node,
                             hash, key, val);
        if (n == NULL) {
            return NULL;
        }

        n2 = hamt_node_bitmap_clone_and_set(self, key_idx, NULL);
        if (n2 == NULL) {
            Py_DECREF(n);
            return NULL;
        }

        Py_XDECREF(n2->b_array[val_idx]);
        n2->b_array[val_idx] = (PyObject *)n;
        return (_PyHamtNode_BaseNode *)n2;
    }
    else {
        int32_t n = hamt_bitcount(self->b_bitmap);
        int32_t key_idx = 2 * idx;
        int32_t val_idx = key_idx + 1;
        Py_ssize_t i;
        PyHamtNode_Bitmap *new_node;
        /* TODO: port ArrayNode: if ( n >= 16) */

        *added_leaf = true;

        new_node = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(2 * (n + 1));
        if (new_node == NULL) {
            return NULL;
        }

        for (i = 0; i < key_idx; i++) {
            Py_XINCREF(self->b_array[i]);
            new_node->b_array[i] = self->b_array[i];
        }

        Py_XINCREF(key);
        new_node->b_array[key_idx] = key;
        Py_XINCREF(val);
        new_node->b_array[val_idx] = val;

        for (i = key_idx; i < Py_SIZE(self); i++) {
            Py_XINCREF(self->b_array[i]);
            new_node->b_array[i + 2] = self->b_array[i];
        }

        new_node->b_bitmap = self->b_bitmap | bit;
        return (_PyHamtNode_BaseNode *)new_node;
    }

    return NULL;
}


static hamt_find_t
hamt_node_bitmap_find(PyHamtNode_Bitmap *self,
                      int32_t shift, int32_t hash,
                      PyObject *key, PyObject **val)
{
    int32_t bit = hamt_bitpos(hash, shift);
    int32_t idx;
    int32_t key_idx;
    int32_t val_idx;
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
        assert(val_or_node != NULL);
        return hamt_node_find((_PyHamtNode_BaseNode *)val_or_node,
                              shift + 5, hash, key, val);
    }

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
    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->b_array[i]);
    }

    return 0;
}


static void
hamt_node_bitmap_dealloc(PyHamtNode_Bitmap *self)
{
    Py_ssize_t len = Py_SIZE(self);

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    if (len > 0) {
        while (--len >= 0) {
            Py_XDECREF(self->b_array[len]);
        }
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
    Py_TRASHCAN_SAFE_END(self)
}


static int
hamt_node_bitmap_dump(PyHamtNode_Bitmap *node,
                      _PyUnicodeWriter *writer, int level)
{
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

    tmp1 = PyLong_FromLong((uint32_t)node->b_bitmap);
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


/////////////////////////////////// Collision Node


static _PyHamtNode_BaseNode *
hamt_node_collision_new(int32_t hash, Py_ssize_t size)
{
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
    node->c_type = Collision;
    node->c_hash = hash;

    _PyObject_GC_TRACK(node);

    return (_PyHamtNode_BaseNode *)node;
}


static hamt_find_t
hamt_node_collision_find_index(PyHamtNode_Collision *self, PyObject *key,
                               Py_ssize_t *idx)
{
    Py_ssize_t i;
    PyObject *el;
    int cmp;

    for (i = 0; i < Py_SIZE(self); i += 2) {
        el = self->c_array[i];

        assert(el != NULL);
        cmp = PyObject_RichCompareBool(key, el, Py_EQ);
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
                          int32_t shift, int32_t hash,
                          PyObject *key, PyObject *val, bool* added_leaf)
{
    if (hash == self->c_hash) {
        Py_ssize_t key_idx = -1;
        Py_ssize_t val_idx;
        hamt_find_t found;
        PyHamtNode_Collision *new_node;
        Py_ssize_t i;

        found = hamt_node_collision_find_index(self, key, &key_idx);
        switch (found) {
            case ERROR:
                return NULL;

            case NOT_FOUND:
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
                assert(key_idx >= 0);
                assert(key_idx < Py_SIZE(self));
                val_idx = key_idx + 1;

                if (self->c_array[val_idx] == val) {
                    Py_INCREF(self);
                    return (_PyHamtNode_BaseNode *)self;
                }

                new_node = (PyHamtNode_Collision *)hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self));
                if (new_node == NULL) {
                    return NULL;
                }

                for (i = 0; i < Py_SIZE(self); i++) {
                    Py_INCREF(self->c_array[i]);
                    new_node->c_array[i] = self->c_array[i];
                }

                Py_DECREF(new_node->c_array[val_idx]);
                Py_INCREF(val);
                new_node->c_array[val_idx] = val;

                return (_PyHamtNode_BaseNode *)new_node;
        }
    }
    else {
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
                         int32_t shift, int32_t hash,
                         PyObject *key, PyObject **val)
{
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
    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->c_array[i]);
    }

    return 0;
}


static void
hamt_node_collision_dealloc(PyHamtNode_Collision *self)
{
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


static int
hamt_node_collision_dump(PyHamtNode_Collision *node,
                         _PyUnicodeWriter *writer, int level)
{
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


/////////////////////////////////// Node Dispatch


static _PyHamtNode_BaseNode *
hamt_node_assoc(_PyHamtNode_BaseNode *node,
                int32_t shift, int32_t hash,
                PyObject *key, PyObject *val, bool* added_leaf)
{
    switch (node->base_type) {
        case Bitmap:
            return hamt_node_bitmap_assoc(
                (PyHamtNode_Bitmap *)node,
                shift, hash, key, val, added_leaf);
        case Collision:
            return hamt_node_collision_assoc(
                (PyHamtNode_Collision *)node,
                shift, hash, key, val, added_leaf);
        default:
            assert(0);
    }
}


static hamt_find_t
hamt_node_find(_PyHamtNode_BaseNode *node,
               int32_t shift, int32_t hash,
               PyObject *key, PyObject **val)
{
    switch (node->base_type) {
        case Bitmap:
            return hamt_node_bitmap_find(
                (PyHamtNode_Bitmap *)node,
                shift, hash, key, val);
        case Collision:
            return hamt_node_collision_find(
                (PyHamtNode_Collision *)node,
                shift, hash, key, val);
        default:
            assert(0);
    }
}


static int
hamt_node_dump(_PyHamtNode_BaseNode *node,
               _PyUnicodeWriter *writer, int level)
{
    switch (node->base_type) {
        case Bitmap:
            return hamt_node_bitmap_dump(
                (PyHamtNode_Bitmap *)node, writer, level);
        case Collision:
            return hamt_node_collision_dump(
                (PyHamtNode_Collision *)node, writer, level);
        default:
            assert(0);
    }
}


static _PyHamtNode_BaseNode *
hamt_node_create(int32_t shift, PyObject *key1, PyObject *val1,
                 int32_t key2_hash, PyObject *key2, PyObject *val2)
{
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


/////////////////////////////////// Hamt Methods


static PyObject *
hamt_py_set(PyHamtObject *self, PyObject *args)
{
    PyObject *key;
    PyObject *val;

    if (!PyArg_ParseTuple(args, "OO:set", &key, &val)) {
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


static PyObject *
hamt_py_dump(PyHamtObject *self, PyObject *args)
{
    return hamt_dump(self);
}


static Py_ssize_t
hamt_py_len(PyHamtObject *self)
{
    return self->h_count;
}


static PyMethodDef PyHamt_methods[] = {
    {"set", (PyCFunction)hamt_py_set, METH_VARARGS, NULL},
    {"get", (PyCFunction)hamt_py_get, METH_VARARGS, NULL},
    {"_dump", (PyCFunction)hamt_py_dump, METH_NOARGS, NULL},
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
