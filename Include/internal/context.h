#ifndef Py_INTERNAL_CONTEXT_H
#define Py_INTERNAL_CONTEXT_H


#define HAMT_ARRAY_NODE_SIZE 32
#define HAMT_MAX_TREE_DEPTH 7


#define PyHamt_Check(o) (Py_TYPE(o) == &PyHamt_Type)


typedef struct {
    PyObject_HEAD
} _PyHamtNode_BaseNode;


typedef struct {
    PyObject_HEAD
    _PyHamtNode_BaseNode *a_array[HAMT_ARRAY_NODE_SIZE];
    Py_ssize_t a_count;
} PyHamtNode_Array;


typedef struct {
    PyObject_VAR_HEAD
    uint32_t b_bitmap;
    PyObject *b_array[1];
} PyHamtNode_Bitmap;


typedef struct {
    PyObject_VAR_HEAD
    int32_t c_hash;
    PyObject *c_array[1];
} PyHamtNode_Collision;


typedef struct {
    PyObject_HEAD
    _PyHamtNode_BaseNode *h_root;
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
    _PyHamtNode_BaseNode *i_nodes[HAMT_MAX_TREE_DEPTH];
    Py_ssize_t i_pos[HAMT_MAX_TREE_DEPTH];
    int8_t i_level;
} PyHamtIteratorState;


typedef struct {
    PyObject_HEAD
    PyHamtObject *hi_obj;
    PyHamtIteratorState hi_iter;
    binaryfunc hi_yield;
} PyHamtIterator;


PyAPI_DATA(PyTypeObject) PyHamt_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_ArrayNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_BitmapNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_CollisionNode_Type;
PyAPI_DATA(PyTypeObject) PyHamtKeys_Type;
PyAPI_DATA(PyTypeObject) PyHamtValues_Type;
PyAPI_DATA(PyTypeObject) PyHamtItems_Type;


#endif /* !Py_INTERNAL_CONTEXT_H */
