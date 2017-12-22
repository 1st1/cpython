#include "Python.h"

PyDoc_STRVAR(module_doc, "Context Variables");

static struct PyModuleDef _contextvarsmodule = {
    PyModuleDef_HEAD_INIT,      /* m_base */
    "_contextvars",             /* m_name */
    module_doc,                 /* m_doc */
    -1,                         /* m_size */
    NULL,                       /* m_methods */
    NULL,                       /* m_slots */
    NULL,                       /* m_traverse */
    NULL,                       /* m_clear */
    NULL,                       /* m_free */
};

PyMODINIT_FUNC
PyInit__contextvars(void)
{
    PyObject *m = PyModule_Create(&_contextvarsmodule);
    if (m == NULL) {
        return NULL;
    }

    Py_INCREF(&PyContext_Type);
    if (PyModule_AddObject(m, "Context",
                           (PyObject *)&PyContext_Type) < 0)
    {
        Py_DECREF(&PyContext_Type);
        return NULL;
    }

    Py_INCREF(&PyContextVar_Type);
    if (PyModule_AddObject(m, "ContextVar",
                           (PyObject *)&PyContextVar_Type) < 0)
    {
        Py_DECREF(&PyContextVar_Type);
        return NULL;
    }

    return m;
}
