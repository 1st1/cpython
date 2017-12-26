#ifndef Py_INTERNAL_HAMT_H
#define Py_INTERNAL_HAMT_H


#define HAMT_ARRAY_NODE_SIZE 32
#define HAMT_MAX_TREE_DEPTH 7


#define PyHamt_Check(o) (Py_TYPE(o) == &_PyHamt_Type)


typedef enum {F_ERROR, F_NOT_FOUND, F_FOUND} _Py_hamt_find_t;
typedef enum {W_ERROR, W_NOT_FOUND, W_EMPTY, W_NEWNODE} _Py_hamt_without_t;
typedef enum {I_ITEM, I_END} _Py_hamt_iter_t;


typedef struct {
    PyObject_VAR_HEAD
} PyHamtNode;


typedef struct {
    PyHamtNode _a_base;
    PyHamtNode *a_array[HAMT_ARRAY_NODE_SIZE];
    Py_ssize_t a_count;
} PyHamtNode_Array;


typedef struct {
    PyHamtNode _b_base;
    uint32_t b_bitmap;
    PyObject *b_array[1];
} PyHamtNode_Bitmap;


typedef struct {
    PyHamtNode _c_base;
    int32_t c_hash;
    PyObject *c_array[1];
} PyHamtNode_Collision;


typedef struct {
    PyObject_HEAD
    PyHamtNode *h_root;
    PyObject *h_weakreflist;
    Py_ssize_t h_count;
} PyHamtObject;


typedef struct {
    /* HAMT is an immutable collection.  Iterators will hold a
       strong reference to it, and every node in the HAMT has
       strong references to its children.

       So for iterators, we can implement zero allocations
       and zero inc/dec ref depth-first iteration.

       The state of the iteration will be stored in this struct.

       - i_nodes: an array of seven pointers to tree nodes
       - i_level: the current node in i_nodes
       - i_pos: an array of positions within nodes in i_nodes.
    */
    PyHamtNode *i_nodes[HAMT_MAX_TREE_DEPTH];
    Py_ssize_t i_pos[HAMT_MAX_TREE_DEPTH];
    int8_t i_level;
} PyHamtIteratorState;


typedef struct {
    PyObject_HEAD
    PyHamtObject *hi_obj;
    PyHamtIteratorState hi_iter;
    binaryfunc hi_yield;
} PyHamtIterator;


PyAPI_DATA(PyTypeObject) _PyHamt_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_ArrayNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_BitmapNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_CollisionNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamtKeys_Type;
PyAPI_DATA(PyTypeObject) _PyHamtValues_Type;
PyAPI_DATA(PyTypeObject) _PyHamtItems_Type;


PyHamtObject *
_PyHamt_New(void);

PyHamtObject *
_PyHamt_Assoc(PyHamtObject *o, PyObject *key, PyObject *val);

PyHamtObject *
_PyHamt_Without(PyHamtObject *o, PyObject *key);

_Py_hamt_find_t
_PyHamt_Find(PyHamtObject *o, PyObject *key, PyObject **val);

int
_PyHamt_Eq(PyHamtObject *v, PyHamtObject *w);

Py_ssize_t
_PyHamt_Len(PyHamtObject *o);

int
_PyHamt_Contains(PyHamtObject *self, PyObject *key);

PyObject *
_PyHamt_GetItem(PyHamtObject *self, PyObject *key);

PyObject *
_PyHamt_Get(PyHamtObject *self, PyObject *key, PyObject *def);

PyObject *
_PyHamt_NewIterKeys(PyHamtObject *o);

PyObject *
_PyHamt_NewIterValues(PyHamtObject *o);

PyObject *
_PyHamt_NewIterItems(PyHamtObject *o);


#endif /* !Py_INTERNAL_HAMT_H */
