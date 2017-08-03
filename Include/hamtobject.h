#ifndef Py_HAMTOBJECT_H
#define Py_HAMTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif


typedef enum {Array, Bitmap, Collision} hamt_node_t;


#define _PyHamtNode_Array_size 32


#define _PyHAMT_HEAD(prefix)            \
    PyObject_VAR_HEAD                   \
    hamt_node_t prefix##_type;


typedef struct {
    _PyHAMT_HEAD(base)
} _PyHamtNode_BaseNode;


typedef struct {
    _PyHAMT_HEAD(a)
    _PyHamtNode_BaseNode *a_array[_PyHamtNode_Array_size];
} PyHamtNode_Array;


typedef struct {
    _PyHAMT_HEAD(b)
    uint32_t b_bitmap;
    PyObject *b_array[1];
} PyHamtNode_Bitmap;


typedef struct {
    _PyHAMT_HEAD(c)
    int32_t c_hash;
    PyObject *c_array[1];
} PyHamtNode_Collision;


typedef struct {
    PyObject_HEAD
    _PyHamtNode_BaseNode *h_root;
    Py_ssize_t h_count;
} PyHamtObject;


#define PyHamt_NodeType(node) (((_PyHamtNode_BaseNode*)(node))->base_type)


PyAPI_DATA(PyTypeObject) PyHamt_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_ArrayNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_BitmapNode_Type;
PyAPI_DATA(PyTypeObject) _PyHamt_CollisionNode_Type;


#define PyHamt_CheckExact(op) (Py_TYPE(op) == &PyHamt_Type)


#ifdef __cplusplus
}
#endif
#endif /* !Py_HAMTOBJECT_H */
