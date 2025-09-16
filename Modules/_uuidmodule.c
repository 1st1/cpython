/*
 * Python UUID module that wraps libuuid or Windows rpcrt4.dll.
 * DCE compatible Universally Unique Identifier library.
 */

#ifndef Py_BUILD_CORE_BUILTIN
#  define Py_BUILD_CORE_MODULE 1
#endif

#include "pyconfig.h"   // Py_GIL_DISABLED
#include "Python.h"
#include <string.h>        // for strncasecmp

#include "pycore_long.h"          // _PyLong_FromByteArray
#include "pycore_pylifecycle.h"   // _PyOS_URandom()

#if defined(HAVE_UUID_H)
  // AIX, FreeBSD, libuuid with pkgconf
  #include <uuid.h>
#elif defined(HAVE_UUID_UUID_H)
  // libuuid without pkgconf
  #include <uuid/uuid.h>
#endif

#ifdef MS_WINDOWS
#include <rpc.h>
#endif

#ifndef MS_WINDOWS


/*[clinic input]
module _uuid
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=7cbed123a45a3859]*/


static PyObject *
py_uuid_generate_time_safe(PyObject *Py_UNUSED(context),
                           PyObject *Py_UNUSED(ignored))
{
    uuid_t uuid;
#ifdef HAVE_UUID_GENERATE_TIME_SAFE
    int res;

    res = uuid_generate_time_safe(uuid);
    return Py_BuildValue("y#i", (const char *) uuid, sizeof(uuid), res);
#elif defined(HAVE_UUID_CREATE)
    uint32_t status;
    uuid_create(&uuid, &status);
# if defined(HAVE_UUID_ENC_BE)
    unsigned char buf[sizeof(uuid)];
    uuid_enc_be(buf, &uuid);
    return Py_BuildValue("y#i", buf, sizeof(uuid), (int) status);
# else
    return Py_BuildValue("y#i", (const char *) &uuid, sizeof(uuid), (int) status);
# endif /* HAVE_UUID_CREATE */
#else /* HAVE_UUID_GENERATE_TIME_SAFE */
    uuid_generate_time(uuid);
    return Py_BuildValue("y#O", (const char *) uuid, sizeof(uuid), Py_None);
#endif /* HAVE_UUID_GENERATE_TIME_SAFE */
}

#else /* MS_WINDOWS */

static PyObject *
py_UuidCreate(PyObject *Py_UNUSED(context),
              PyObject *Py_UNUSED(ignored))
{
    UUID uuid;
    RPC_STATUS res;

    Py_BEGIN_ALLOW_THREADS
    res = UuidCreateSequential(&uuid);
    Py_END_ALLOW_THREADS

    switch (res) {
    case RPC_S_OK:
    case RPC_S_UUID_LOCAL_ONLY:
    case RPC_S_UUID_NO_ADDRESS:
        /*
        All success codes, but the latter two indicate that the UUID is random
        rather than based on the MAC address. If the OS can't figure this out,
        neither can we, so we'll take it anyway.
        */
        return Py_BuildValue("y#", (const char *)&uuid, sizeof(uuid));
    }
    PyErr_SetFromWindowsErr(res);
    return NULL;
}

static int
py_windows_has_stable_node(void)
{
    UUID uuid;
    RPC_STATUS res;
    Py_BEGIN_ALLOW_THREADS
    res = UuidCreateSequential(&uuid);
    Py_END_ALLOW_THREADS
    return res == RPC_S_OK;
}
#endif /* MS_WINDOWS */


typedef struct uuidobject {
    PyObject_HEAD
    char bytes[16];
    PyObject *cached_int;  // Cached int representation
} uuidobject;


/* State of the _uuid module */
typedef struct {
    PyTypeObject *UuidType;

    PyObject *safe_uuid_safe;
    PyObject *safe_uuid_unsafe;
    PyObject *safe_uuid_unknown;
} uuid_state;

#include "clinic/_uuidmodule.c.h"

/*[clinic input]
class _uuid.UUIDBase "uuidobject *" "&UuidType"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=f8e4c40a12276445]*/

// Forward declarations
static int from_hex(uuidobject *self, PyObject *hex);
static int from_bytes_le(uuidobject *self, Py_buffer *bytes_le);

/*[clinic input]
_uuid.UUIDBase.__init__

    hex: 'U' = NULL
    bytes: 'y*' = None
    bytes_le: 'y*' = None
    fields: object = NULL
    int: object = NULL

UUIDBase is a fast base implementation type for uuid.UUID.
[clinic start generated code]*/

static int
_uuid_UUIDBase___init___impl(uuidobject *self, PyObject *hex,
                             Py_buffer *bytes, Py_buffer *bytes_le,
                             PyObject *fields, PyObject *int_value)
/*[clinic end generated code: output=c1e915fca9509416 input=dfa3946b97a91fc7]*/

{
    int passed = 0;
    if (hex != NULL) passed++;
    if (bytes->obj != NULL) passed++;
    if (bytes_le->obj != NULL) passed++;
    if (fields != NULL) passed++;
    if (int_value != NULL) passed++;
    if (passed != 1) {
        PyErr_SetString(
            PyExc_TypeError,
            "one of the hex, bytes, bytes_le, fields, or int arguments must be given"
        );
        return -1;
    }

    if (hex != NULL) {
        if (from_hex(self, hex) < 0) {
            return -1;
        }
        return 0;
    }

    // Initialize from bytes
    if (bytes->obj != NULL) {
        if (bytes->len != 16) {
            PyErr_SetString(
                PyExc_ValueError,
                "bytes is not a 16-char string"
            );
            return -1;
        }
        memcpy(self->bytes, bytes->buf, 16);
        return 0;
    }
    if (bytes_le->obj != NULL) {
        if (from_bytes_le(self, bytes_le) < 0) {
            return -1;
        }
        return 0;
    }
    if (fields != NULL) {
        PyErr_SetString(PyExc_NotImplementedError,
                        "fields initialization not yet implemented");
        return -1;
    }
    if (int_value != NULL) {
        PyErr_SetString(PyExc_NotImplementedError,
                        "int initialization not yet implemented");
        return -1;
    }

    // Should never reach here due to passed != 4 check above
    return -1;
}

// Lookup table for hex character to value conversion
// -1 for invalid characters
static const int8_t _hextable[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1, 0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,-1,10,11,12,13,14,15,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

static int
from_hex(uuidobject *self, PyObject *hex)
{
    Py_ssize_t size;
    const char *start = PyUnicode_AsUTF8AndSize(hex, &size);
    if (start == NULL) {
        return -1;
    }

    uint8_t ch;
    uint8_t acc, acc_set;
    int8_t part;
    int i, j;

    // Reimplement `hex = hex.replace('urn:', '').replace('uuid:', '')`
    if (size > 0 && start[0] == 'u') {
        if (size >= 9 && strncmp(start, "urn:uuid:", 9) == 0) {
            start += 9;
            size -= 9;
        }
        else if (size >= 4 && strncmp(start, "urn:", 4) == 0) {
            start += 4;
            size -= 4;
        }
        else if (size >= 5 && strncmp(start, "uuid:", 5) == 0) {
            start += 5;
            size -= 5;
        }
    }

    // Reimplement `hex = hex.strip('{}')`
    if (size >= 1 && start[0] == '{') {
        start++;
        size -= 1;
    }
    if (size >= 1 && start[size - 1] == '}') {
        size -= 1;
    }

    if (size < 32) {
        PyErr_SetString(
            PyExc_ValueError,
            "badly formed hexadecimal UUID string"
        );
        return -1;
    }

    acc_set = 0;
    j = 0;

    for (i = 0; i < size; i++) {
        ch = (uint8_t)start[i];

        if (ch == '-') {
            continue;
        }

        part = _hextable[ch];
        if (part == -1) {
            PyErr_SetString(
                PyExc_ValueError,
                "badly formed hexadecimal UUID string"
            );
            return -1;
        }

        if (acc_set) {
            acc |= (uint8_t)part;
            self->bytes[j] = (char)acc;
            acc_set = 0;
            j++;
        }
        else {
            acc = (uint8_t)part << 4;
            acc_set = 1;
        }

        if (j > 16 || (j == 16 && acc_set)) {
            PyErr_Format(PyExc_ValueError,
                "invalid UUID '%s': decodes to more than 16 bytes",
                hex);
            return -1;
        }
    }

    if (j != 16) {
        PyErr_Format(PyExc_ValueError,
            "invalid UUID '%s': decodes to less than 16 bytes",
            hex);
        return -1;
    }

    return 0;
}

static int
from_bytes_le(uuidobject *self, Py_buffer *bytes_le)
{
    if (bytes_le->len != 16) {
        PyErr_SetString(PyExc_ValueError,
            "bytes_le is not a 16-char string");
        return -1;
    }

    // Convert from little-endian to big-endian UUID format
    // UUID fields in little-endian order need to be byte-swapped:
    // - time_low (4 bytes)
    // - time_mid (2 bytes)
    // - time_hi_version (2 bytes)
    // - clock_seq_hi_variant (1 byte) - no swap needed
    // - clock_seq_low (1 byte) - no swap needed
    // - node (6 bytes) - no swap needed

    unsigned char *src = (unsigned char *)bytes_le->buf;
    unsigned char *dst = (unsigned char *)self->bytes;

    // Swap time_low (bytes 0-3)
    dst[0] = src[3];
    dst[1] = src[2];
    dst[2] = src[1];
    dst[3] = src[0];

    // Swap time_mid (bytes 4-5)
    dst[4] = src[5];
    dst[5] = src[4];

    // Swap time_hi_version (bytes 6-7)
    dst[6] = src[7];
    dst[7] = src[6];

    // Copy clock_seq and node as-is (bytes 8-15)
    memcpy(dst + 8, src + 8, 8);

    return 0;
}

static PyObject *
get_int(uuidobject *self)
{
    if (self->cached_int == NULL) {
        self->cached_int = _PyLong_FromByteArray((unsigned char *)self->bytes, 16, 0, 0);
        if (self->cached_int == NULL) {
            return NULL;
        }
    }
    return Py_XNewRef(self->cached_int);
}

static inline uuid_state *
get_uuid_state(PyObject *mod)
{
    uuid_state *state = PyModule_GetState(mod);
    assert(state != NULL);
    return state;
}


static PyObject *
Uuid_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    uuidobject *self;
    self = (uuidobject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->cached_int = NULL;
    memset(self->bytes, 0, 16);

    return (PyObject *)self;
}

static void
Uuid_dealloc(PyObject *obj)
{
    uuidobject *uuid = (uuidobject *)obj;
    Py_XDECREF(uuid->cached_int);
    PyObject_Free(uuid);
}


static PyObject *
Uuid_get_int(uuidobject *self, void *closure)
{
    return get_int(self);
}

static PyGetSetDef Uuid_getset[] = {
    {"int", (getter)Uuid_get_int, NULL, "UUID as a 128-bit integer", NULL},
    {NULL}  /* Sentinel */
};

static PyMethodDef Uuid_methods[] = {
    {NULL, NULL}        /* Sentinel */
};


static PyType_Slot Uuid_slots[] = {
    {Py_tp_new, Uuid_new},
    {Py_tp_dealloc, Uuid_dealloc},
    {Py_tp_getattro, PyObject_GenericGetAttr},
    {Py_tp_methods, Uuid_methods},
    {Py_tp_getset, Uuid_getset},
    {Py_tp_init, _uuid_UUIDBase___init__},
    {Py_tp_doc, (void *)_uuid_UUIDBase___init____doc__},
    {0, NULL},
};


static PyType_Spec Uuid_spec = {
    .name = "_uuid.UUIDBase",
    .basicsize = sizeof(uuidobject),
    .flags = (
        Py_TPFLAGS_DEFAULT
        | Py_TPFLAGS_BASETYPE
        | Py_TPFLAGS_IMMUTABLETYPE
    ),
    .slots = Uuid_slots,
};


static int
module_traverse(PyObject *mod, visitproc visit, void *arg)
{
    uuid_state *state = get_uuid_state(mod);
    Py_VISIT(state->UuidType);
    return 0;
}

static int
module_clear(PyObject *mod)
{
    uuid_state *state = get_uuid_state(mod);
    Py_CLEAR(state->UuidType);
    return 0;
}

static void
module_free(void *mod)
{
    (void)module_clear((PyObject *)mod);
}


static int
uuid_exec(PyObject *module)
{
    uuid_state *state = get_uuid_state(module);
    PyObject *uuid_mod = NULL;
    PyObject *safe_uuid = NULL;

#define ADD_INT(NAME, VALUE)                                        \
    do {                                                            \
        if (PyModule_AddIntConstant(module, (NAME), (VALUE)) < 0) { \
            goto fail;                                              \
        }                                                           \
    } while (0)

    assert(sizeof(uuid_t) == 16);
#if defined(MS_WINDOWS)
    ADD_INT("has_uuid_generate_time_safe", 0);
#elif defined(HAVE_UUID_GENERATE_TIME_SAFE)
    ADD_INT("has_uuid_generate_time_safe", 1);
#else
    ADD_INT("has_uuid_generate_time_safe", 0);
#endif

#if defined(MS_WINDOWS)
    ADD_INT("has_stable_extractable_node", py_windows_has_stable_node());
#elif defined(HAVE_UUID_GENERATE_TIME_SAFE_STABLE_MAC)
    ADD_INT("has_stable_extractable_node", 1);
#else
    ADD_INT("has_stable_extractable_node", 0);
#endif

#undef ADD_INT

    state->UuidType = (PyTypeObject *)PyType_FromMetaclass(
        NULL,
        module,
        &Uuid_spec,
        NULL
    );
    if (state->UuidType == NULL) {
        goto fail;
    }
    if (PyModule_AddType(module, state->UuidType) < 0) {
        goto fail;
    }

    uuid_mod = PyImport_ImportModule("uuid");
    if (uuid_mod == NULL) {
        goto fail;
    }
    safe_uuid = PyObject_GetAttrString(uuid_mod, "SafeUUID");
    if (safe_uuid == NULL) {
        goto fail;
    }
    state->safe_uuid_safe = PyObject_GetAttrString(safe_uuid, "safe");
    if (state->safe_uuid_safe == NULL) {
        goto fail;
    }
    state->safe_uuid_unsafe = PyObject_GetAttrString(safe_uuid, "unsafe");
    if (state->safe_uuid_unsafe == NULL) {
        goto fail;
    }
    state->safe_uuid_unknown = PyObject_GetAttrString(safe_uuid, "unknown");
    if (state->safe_uuid_unknown == NULL) {
        goto fail;
    }
    Py_CLEAR(safe_uuid);
    Py_CLEAR(uuid_mod);

    return 0;

fail:
    Py_CLEAR(safe_uuid);
    Py_CLEAR(uuid_mod);
    return -1;
}

static PyMethodDef uuid_methods[] = {
#if defined(HAVE_UUID_UUID_H) || defined(HAVE_UUID_H)
    {"generate_time_safe", py_uuid_generate_time_safe, METH_NOARGS, NULL},
#endif
#if defined(MS_WINDOWS)
    {"UuidCreate", py_UuidCreate, METH_NOARGS, NULL},
#endif
    {NULL, NULL, 0, NULL}           /* sentinel */
};

static PyModuleDef_Slot uuid_slots[] = {
    {Py_mod_exec, uuid_exec},
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
    {0, NULL}
};

static struct PyModuleDef uuidmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_uuid",
    .m_size = sizeof(uuid_state),
    .m_methods = uuid_methods,
    .m_traverse = module_traverse,
    .m_clear = module_clear,
    .m_slots = uuid_slots,
    .m_free = module_free,
};

PyMODINIT_FUNC
PyInit__uuid(void)
{
    return PyModuleDef_Init(&uuidmodule);
}
