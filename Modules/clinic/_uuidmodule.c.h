/*[clinic input]
preserve
[clinic start generated code]*/

#if defined(Py_BUILD_CORE) && !defined(Py_BUILD_CORE_MODULE)
#  include "pycore_gc.h"          // PyGC_Head
#  include "pycore_runtime.h"     // _Py_ID()
#endif
#include "pycore_modsupport.h"    // _PyArg_UnpackKeywords()

PyDoc_STRVAR(_uuid_UUIDBase___init____doc__,
"UUIDBase(hex=<unrepresentable>, bytes=None, bytes_le=None,\n"
"         fields=<unrepresentable>, int=<unrepresentable>,\n"
"         version=<unrepresentable>, *, is_safe=<unrepresentable>)\n"
"--\n"
"\n"
"UUIDBase is a fast base implementation type for uuid.UUID.");

static int
_uuid_UUIDBase___init___impl(uuidobject *self, PyObject *hex,
                             Py_buffer *bytes, Py_buffer *bytes_le,
                             PyObject *fields, PyObject *int_value,
                             PyObject *version, PyObject *is_safe);

static int
_uuid_UUIDBase___init__(PyObject *self, PyObject *args, PyObject *kwargs)
{
    int return_value = -1;
    #if defined(Py_BUILD_CORE) && !defined(Py_BUILD_CORE_MODULE)

    #define NUM_KEYWORDS 7
    static struct {
        PyGC_Head _this_is_not_used;
        PyObject_VAR_HEAD
        Py_hash_t ob_hash;
        PyObject *ob_item[NUM_KEYWORDS];
    } _kwtuple = {
        .ob_base = PyVarObject_HEAD_INIT(&PyTuple_Type, NUM_KEYWORDS)
        .ob_hash = -1,
        .ob_item = { &_Py_ID(hex), &_Py_ID(bytes), &_Py_ID(bytes_le), &_Py_ID(fields), &_Py_ID(int), &_Py_ID(version), &_Py_ID(is_safe), },
    };
    #undef NUM_KEYWORDS
    #define KWTUPLE (&_kwtuple.ob_base.ob_base)

    #else  // !Py_BUILD_CORE
    #  define KWTUPLE NULL
    #endif  // !Py_BUILD_CORE

    static const char * const _keywords[] = {"hex", "bytes", "bytes_le", "fields", "int", "version", "is_safe", NULL};
    static _PyArg_Parser _parser = {
        .keywords = _keywords,
        .fname = "UUIDBase",
        .kwtuple = KWTUPLE,
    };
    #undef KWTUPLE
    PyObject *argsbuf[7];
    PyObject * const *fastargs;
    Py_ssize_t nargs = PyTuple_GET_SIZE(args);
    Py_ssize_t noptargs = nargs + (kwargs ? PyDict_GET_SIZE(kwargs) : 0) - 0;
    PyObject *hex = NULL;
    Py_buffer bytes = {NULL, NULL};
    Py_buffer bytes_le = {NULL, NULL};
    PyObject *fields = NULL;
    PyObject *int_value = NULL;
    PyObject *version = NULL;
    PyObject *is_safe = NULL;

    fastargs = _PyArg_UnpackKeywords(_PyTuple_CAST(args)->ob_item, nargs, kwargs, NULL, &_parser,
            /*minpos*/ 0, /*maxpos*/ 6, /*minkw*/ 0, /*varpos*/ 0, argsbuf);
    if (!fastargs) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (fastargs[0]) {
        if (!PyUnicode_Check(fastargs[0])) {
            _PyArg_BadArgument("UUIDBase", "argument 'hex'", "str", fastargs[0]);
            goto exit;
        }
        hex = fastargs[0];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[1]) {
        if (PyObject_GetBuffer(fastargs[1], &bytes, PyBUF_SIMPLE) != 0) {
            goto exit;
        }
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[2]) {
        if (PyObject_GetBuffer(fastargs[2], &bytes_le, PyBUF_SIMPLE) != 0) {
            goto exit;
        }
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[3]) {
        fields = fastargs[3];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[4]) {
        int_value = fastargs[4];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (fastargs[5]) {
        version = fastargs[5];
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
skip_optional_pos:
    if (!noptargs) {
        goto skip_optional_kwonly;
    }
    is_safe = fastargs[6];
skip_optional_kwonly:
    return_value = _uuid_UUIDBase___init___impl((uuidobject *)self, hex, &bytes, &bytes_le, fields, int_value, version, is_safe);

exit:
    /* Cleanup for bytes */
    if (bytes.obj) {
       PyBuffer_Release(&bytes);
    }
    /* Cleanup for bytes_le */
    if (bytes_le.obj) {
       PyBuffer_Release(&bytes_le);
    }

    return return_value;
}
/*[clinic end generated code: output=d22453feb1be1c5d input=a9049054013a1b77]*/
