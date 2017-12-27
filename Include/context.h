#ifndef Py_CONTEXT_H
#define Py_CONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_LIMITED_API


PyAPI_DATA(PyTypeObject) PyContext_Type;
typedef struct _pycontextobject PyContext;

PyAPI_DATA(PyTypeObject) PyContextVar_Type;
typedef struct _pycontextvarobject PyContextVar;

PyAPI_DATA(PyTypeObject) PyContextToken_Type;
typedef struct _pycontexttokenobject PyContextToken;


#define PyContext_CheckExact(o) (Py_TYPE(o) == &PyContext_Type)
#define PyContextVar_CheckExact(o) (Py_TYPE(o) == &PyContextVar_Type)
#define PyContextToken_CheckExact(o) (Py_TYPE(o) == &PyContextToken_Type)


PyAPI_FUNC(PyContext *) PyContext_New(void);
PyAPI_FUNC(PyContext *) PyContext_Copy(void);

PyAPI_FUNC(int) PyContext_Enter(PyContext *);
PyAPI_FUNC(int) PyContext_Exit(PyContext *);


PyAPI_FUNC(PyContextVar *) PyContextVar_New(const char *, PyObject *);
PyAPI_FUNC(int) PyContextVar_Get(PyContextVar *, PyObject *, PyObject **);
PyAPI_FUNC(PyContextToken *) PyContextVar_Set(PyContextVar *, PyObject *);
PyAPI_FUNC(int) PyContextVar_Reset(PyContextVar *, PyContextToken *);


/* This method is exposed only for CPython tests. Don not use it. */
PyAPI_FUNC(PyObject *) _PyContext_NewHamtForTests(void);


PyAPI_FUNC(int) PyContext_ClearFreeList(void);


#endif /* !Py_LIMITED_API */

#ifdef __cplusplus
}
#endif
#endif /* !Py_CONTEXT_H */
