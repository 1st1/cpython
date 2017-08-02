#include <stdbool.h>
#include "Python.h"


static PyHamtObject *
_hamt_new(void);

static _PyHamtNode_BaseNode *
hamt_node_assoc(_PyHamtNode_BaseNode *node,
                int32_t shift, int32_t hash,
                PyObject *key, PyObject *val, bool* added_leaf);


/* Returns -1 on error */
static inline int32_t
hamt_hash(PyObject *o)
{
    Py_hash_t hash = PyObject_Hash(o);

    if (hash == -1) {
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
hamt_bitindex(int32_t bitmap, int32_t bit)
{
    return __builtin_popcount(bitmap & (bit - 1));
}


/////////////////////////////////// Bitmap Node


static _PyHamtNode_BaseNode *
hamt_node_bitmap_new(Py_ssize_t size)
{
    PyHamtNode_Bitmap *node;
    Py_ssize_t i;

    assert(size >= 0);

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
        Py_INCREF(o->b_array[i]);
        clone->b_array[i] = o->b_array[i];
    }

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

    if ((self->b_bitmap & bit) !=0) {
        int32_t key_idx = 2 * idx;
        int32_t val_idx = key_idx + 1;

        PyObject *key_or_null = self->b_array[key_idx];
        PyObject *val_or_node = self->b_array[val_idx];

        if (key_or_null == NULL) {
            _PyHamtNode_BaseNode *n;
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

        comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
        if (comp_err < 0) {
            return NULL;
        }
        if (comp_err) {  /* key != key_or_null */
            if (val == val_or_node) {
                Py_INCREF(self);
                return (_PyHamtNode_BaseNode *)self;
            }

            return (_PyHamtNode_BaseNode *)hamt_node_bitmap_clone_and_set(
                self, val_idx, val);
        }

        *added_leaf = true;
    }

    return NULL;
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

        default:
            assert(0);
    }
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


///////////////////////////////////


PyTypeObject PyHamt_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt",
    sizeof(PyHamtObject),
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
    sizeof(PyHamtNode_Collision),
};
