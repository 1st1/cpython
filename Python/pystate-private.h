#ifndef Py_PYSTATE_PRIVATE_H
#define Py_PYSTATE_PRIVATE_H
#ifdef __cplusplus
extern "C" {
#endif


struct _execcontextdata {
    PyObject_HEAD
    PyObject *ec_items;
};  /* PyExecContextData */


struct _execcontextitem {
    PyObject_HEAD
    PyObject *ei_desc;
};  /* PyExecContextItem */


#ifdef __cplusplus
}
#endif
#endif /* !Py_PYSTATE_PRIVATE_H */
