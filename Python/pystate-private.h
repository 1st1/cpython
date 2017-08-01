#ifndef Py_PYSTATE_PRIVATE_H
#define Py_PYSTATE_PRIVATE_H
#ifdef __cplusplus
extern "C" {
#endif


struct _execcontextdata {
    PyObject_HEAD
    PyObject *ec_items;
};


#ifdef __cplusplus
}
#endif
#endif /* !Py_PYSTATE_PRIVATE_H */
