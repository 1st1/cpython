#ifndef Py_INTERNAL_HAMT_H
#define Py_INTERNAL_HAMT_H


#define HAMT_ARRAY_NODE_SIZE 32
#define HAMT_MAX_TREE_DEPTH 7


#define PyHamt_Check(o) (Py_TYPE(o) == &_PyHamt_Type)


/* Abstract tree node.  We use it mostly to have a typed pointer
   to a tree node. */
typedef struct {
    PyObject_VAR_HEAD
} PyHamtNode;


/* Array HAMT node. */
typedef struct {
    PyHamtNode _a_base;
    PyHamtNode *a_array[HAMT_ARRAY_NODE_SIZE];
    Py_ssize_t a_count;
} PyHamtNode_Array;


/* Bitmap HAMT node. */
typedef struct {
    PyHamtNode _b_base;
    uint32_t b_bitmap;
    PyObject *b_array[1];
} PyHamtNode_Bitmap;


/* Collision HAMT node. */
typedef struct {
    PyHamtNode _c_base;
    int32_t c_hash;
    PyObject *c_array[1];
} PyHamtNode_Collision;


/* An HAMT immutable mapping collection. */
typedef struct {
    PyObject_HEAD
    PyHamtNode *h_root;
    PyObject *h_weakreflist;
    Py_ssize_t h_count;
} PyHamtObject;


/* A struct to hold the state of depth-first traverse of the tree.

   HAMT is an immutable collection.  Iterators will hold a strong reference
   to it, and every node in the HAMT has strong references to its children.

   So for iterators, we can implement zero allocations and zero reference
   inc/dec depth-first iteration.

   - i_nodes: an array of seven pointers to tree nodes
   - i_level: the current node in i_nodes
   - i_pos: an array of positions within nodes in i_nodes.
*/
typedef struct {
    PyHamtNode *i_nodes[HAMT_MAX_TREE_DEPTH];
    Py_ssize_t i_pos[HAMT_MAX_TREE_DEPTH];
    int8_t i_level;
} PyHamtIteratorState;


/* Base iterator object.

   Contains the iteration state, a pointer to the HAMT tree,
   and a pointer to the 'yield function'.  The latter is a simple
   function that returns a key/value tuple for the 'Items' iterator,
   just a key for the 'Keys' iterator, and a value for the 'Values'
   iterator.
*/
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


/* Create a new HAMT immutable mapping. */
PyHamtObject *
_PyHamt_New(void);

/* Return a new collection based on "o", but with an additional
   key/val pair. */
PyHamtObject *
_PyHamt_Assoc(PyHamtObject *o, PyObject *key, PyObject *val);

/* Return a new collection based on "o", but without "key". */
PyHamtObject *
_PyHamt_Without(PyHamtObject *o, PyObject *key);

/* Find "key" in the "o" collection.

   Return:
   - -1: An error ocurred.
   - 0: "key" wasn't found in "o".
   - 1: "key" is in "o"; "*val" is set to its value.
*/
int
_PyHamt_Find(PyHamtObject *o, PyObject *key, PyObject **val);

/* Check if "v" is equal to "w".

   Return:
   - 0: v != w
   - 1: v == w
   - -1: An error occurred.
*/
int
_PyHamt_Eq(PyHamtObject *v, PyHamtObject *w);

/* Return the size of "o"; equivalent of "len(o)". */
Py_ssize_t
_PyHamt_Len(PyHamtObject *o);

/* Return a Keys iterator over "o". */
PyObject *
_PyHamt_NewIterKeys(PyHamtObject *o);

/* Return a Values iterator over "o". */
PyObject *
_PyHamt_NewIterValues(PyHamtObject *o);

/* Return a Items iterator over "o". */
PyObject *
_PyHamt_NewIterItems(PyHamtObject *o);


#endif /* !Py_INTERNAL_HAMT_H */
