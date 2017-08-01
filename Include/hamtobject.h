#ifndef Py_HAMTOBJECT_H
#define Py_HAMTOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    PyObject_HEAD
} PyHamtObject;


PyAPI_DATA(PyTypeObject) PyHamt_Type;


#define PyHamt_CheckExact(op) (Py_TYPE(op) == &PyHamt_Type)


#ifdef __cplusplus
}
#endif
#endif /* !Py_HAMTOBJECT_H */
