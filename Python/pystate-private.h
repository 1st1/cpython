#ifndef Py_PYSTATE_PRIVATE_H
#define Py_PYSTATE_PRIVATE_H
#ifdef __cplusplus
extern "C" {
#endif


struct _execcontextobject {
    PyObject_HEAD
    int ec_copy_on_write;
    struct _execcontextobject *ec_prev;
    PyObject *ec_items;
};


int
_PyExecutionContext_SET(PyExecutionContext *);

int
_PyExecutionContext_Fork(PyExecutionContext **);


#ifdef __cplusplus
}
#endif
#endif /* !Py_PYSTATE_PRIVATE_H */
