/*[clinic input]
preserve
[clinic start generated code]*/

PyDoc_STRVAR(_contextvars_get_context__doc__,
"get_context($module, /)\n"
"--\n"
"\n");

#define _CONTEXTVARS_GET_CONTEXT_METHODDEF    \
    {"get_context", (PyCFunction)_contextvars_get_context, METH_NOARGS, _contextvars_get_context__doc__},

static PyObject *
_contextvars_get_context_impl(PyObject *module);

static PyObject *
_contextvars_get_context(PyObject *module, PyObject *Py_UNUSED(ignored))
{
    return _contextvars_get_context_impl(module);
}
/*[clinic end generated code: output=7d640716db19e23b input=a9049054013a1b77]*/
