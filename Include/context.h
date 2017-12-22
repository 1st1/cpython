#ifndef Py_CONTEXT_H
#define Py_CONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_LIMITED_API


/* This method is exposed only for CPython tests. Don not use it. */
PyAPI_FUNC(PyObject *) _PyContext_NewHamtForTests(void);


PyAPI_DATA(PyTypeObject) PyContext_Type;
typedef struct _pycontextobject PyContext;


PyAPI_DATA(PyTypeObject) PyContextVar_Type;
typedef struct _pycontextvarobject PyContextVar;


#define PyContext_CheckExact(o) (Py_TYPE(o) == &PyContext_Type)
#define PyContextVar_CheckExact(o) (Py_TYPE(o) == &PyContextVar_Type)


#endif /* !Py_LIMITED_API */

#ifdef __cplusplus
}
#endif
#endif /* !Py_CONTEXT_H */
