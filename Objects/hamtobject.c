#include "Python.h"


static PyHamtNode_Bitmap *
hamt_new_node_bitmap()
{

}


static int
hamt_node_clear(_PyHamtNode_BaseNode *self)
{
    return 0;
}


static int
hamt_node_traverse(_PyHamtNode_BaseNode *self, visitproc visit, void *arg)
{
    return 0;
}


static void
hamt_node_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    (void)hamt_node_clear((_PyHamtNode_BaseNode*)self);
    Py_TYPE(self)->tp_free(self);
}


///////////////////////////////////


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
    h->h_root = NULL;
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


#define DEFINE_NODE_TYPE(name)                                  \
    PyTypeObject _PyHamt_##name##Node_Type = {                  \
        PyVarObject_HEAD_INIT(&PyType_Type, 0)                  \
        #name,                                                  \
        sizeof(PyHamtNode_##name),                              \
        .tp_dealloc = (destructor)hamt_node_dealloc,            \
        .tp_getattro = PyObject_GenericGetAttr,                 \
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,    \
        .tp_traverse = (traverseproc)hamt_node_traverse,        \
        .tp_clear = (inquiry)hamt_node_clear,

#define END_NODE_TYPE                                           \
    }


DEFINE_NODE_TYPE(Array)
END_NODE_TYPE;


DEFINE_NODE_TYPE(Bitmap)
END_NODE_TYPE;


DEFINE_NODE_TYPE(Collision)
END_NODE_TYPE;
