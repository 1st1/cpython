#include "Python.h"


static int
hamt_clear(PyHamtObject *self)
{
    return 0;
}


static int
hamt_traverse(PyHamtObject *self, visitproc visit, void *arg)
{
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
    return self;
}


static void
hamt_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    (void)hamt_clear(self);
    Py_TYPE(self)->tp_free(self);
}


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
