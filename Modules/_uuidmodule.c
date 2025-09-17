// UUID accelerator base type.

#ifndef Py_BUILD_CORE_BUILTIN
#  define Py_BUILD_CORE_MODULE 1
#endif

#include "pyconfig.h"   // Py_GIL_DISABLED
#include "Python.h"
#include <string.h>        // for strncasecmp
#include "structmember.h"  // for PyMemberDef

#include "pycore_long.h"          // _PyLong_FromByteArray, _PyLong_AsByteArray
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
    uint8_t bytes[16];
    PyObject *cached_int;  // Cached int representation
    PyObject *is_safe;     // SafeUUID enum value
    PyObject *weakreflist; // Weak reference list
    Py_hash_t cached_hash;        // Hash value
} uuidobject;


// UUID Structure per RFC 9562:
//
// A UUID is 128 bits (16 bytes) represented as:
//
// String:       xx xx xx xx - xx xx - Mx xx - Nx xx - xx xx xx xx xx xx
// Byte pos:     0  1  2  3    4  5    6  7    8  9    10 11 12 13 14 15
//               ^^^^^^^^^^^   ^^^^^   ^^^^^   ^^^^^   ^^^^^^^^^^^^^^^^^
//                time_low      mid     hi      seq          node
//
// Byte Layout (big-endian):
//
// Bytes 0-3:   time_low                 (32 bits)
// Bytes 4-5:   time_mid                 (16 bits)
// Bytes 6-7:   time_hi_and_version      (16 bits)
// Bytes 8-9:   clock_seq_and_variant    (16 bits)
// Bytes 10-15: node                     (48 bits)
//
// Version field is located in byte 6; most significant 4 bits:
//
// Variant field is located in byte 8; most significant variable bits:
//   0xxx: Reserved for NCS compatibility
//   10xx: RFC 4122/9562 (standard)
//   110x: Reserved for Microsoft compatibility
//   111x: Reserved for future definition


/* State of the _uuid module */
typedef struct {
    PyTypeObject *UuidType;

    PyObject *safe_uuid;
    PyObject *safe_uuid_safe;
    PyObject *safe_uuid_unsafe;
    PyObject *safe_uuid_unknown;

    PyObject *uint128_max;
    PyObject *from_fields_func;

    PyObject *reserved_ncs;
    PyObject *rfc_4122;
    PyObject *reserved_microsoft;
    PyObject *reserved_future;
} uuid_state;

#include "clinic/_uuidmodule.c.h"

/*[clinic input]
class _uuid.UUIDBase "uuidobject *" "&UuidType"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=f8e4c40a12276445]*/

// Forward declarations
static int from_hex(uuidobject *self, PyObject *hex);
static int from_bytes_le(uuidobject *self, Py_buffer *bytes_le);
static int from_int(uuidobject *self, PyObject *int_value);
static int from_fields(uuidobject *self, PyObject *fields);

static inline uuid_state *
get_uuid_state(PyObject *mod)
{
    uuid_state *state = PyModule_GetState(mod);
    assert(state != NULL);
    return state;
}

static inline uuid_state *
get_uuid_state_by_cls(PyTypeObject *cls)
{
    uuid_state *state = (uuid_state *)PyType_GetModuleState(cls);
    assert(state != NULL);
    return state;
}

/*[clinic input]
_uuid.UUIDBase.__init__

    hex: 'U' = NULL
    bytes: 'y*' = None
    bytes_le: 'y*' = None
    fields: object = NULL
    int: object = NULL
    version: object = NULL
    *
    is_safe: object = NULL

UUIDBase is a fast base implementation type for uuid.UUID.
[clinic start generated code]*/

static int
_uuid_UUIDBase___init___impl(uuidobject *self, PyObject *hex,
                             Py_buffer *bytes, Py_buffer *bytes_le,
                             PyObject *fields, PyObject *int_value,
                             PyObject *version, PyObject *is_safe)
/*[clinic end generated code: output=0620020f183160d6 input=8a7375a0f9275225]*/

{
    uuid_state *state = get_uuid_state_by_cls(Py_TYPE(self));

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
    }

    if (hex != NULL) {
        if (from_hex(self, hex) < 0) {
            return -1;
        }
    }
    else if (bytes->obj != NULL) {
        if (bytes->len != 16) {
            PyErr_SetString(
                PyExc_ValueError,
                "bytes is not a 16-char string"
            );
            return -1;
        }
        memcpy(self->bytes, bytes->buf, 16);
    }
    else if (bytes_le->obj != NULL) {
        if (from_bytes_le(self, bytes_le) < 0) {
            return -1;
        }
    }
    else if (fields != NULL) {
        if (from_fields(self, fields) < 0) {
            return -1;
        }
    }
    else if (int_value != NULL) {
        if (from_int(self, int_value) < 0) {
            return -1;
        }
    }
    else {
        Py_UNREACHABLE();
    }

    if (version != NULL) {
        // Version must be an integer between 1 and 8
        long version_num = PyLong_AsLong(version);
        if (version_num == -1 && PyErr_Occurred()) {
            return -1;
        }
        if (version_num < 1 || version_num > 8) {
            PyErr_SetString(PyExc_ValueError, "illegal version number");
            return -1;
        }

        // Clear variant bits (keep only lower 6 bits of byte 8)
        self->bytes[8] &= 0x3f;  // 0011 1111

        // Clear version bits (keep only lower 4 bits of byte 6)
        self->bytes[6] &= 0x0f;  // 0000 1111

        // Set the variant to RFC 4122/9562 (binary 10xx xxxx)
        self->bytes[8] |= 0x80;  // 1000 0000

        // Set the version number (upper 4 bits of byte 6)
        self->bytes[6] |= (version_num << 4);

        // Clear cached_int if it exists since we modified the bytes
        Py_CLEAR(self->cached_int);
    }

    if (is_safe != NULL) {
        // Validate by calling SafeUUID(is_safe) to ensure it's a valid enum member
        PyObject *validated = PyObject_CallOneArg(state->safe_uuid, is_safe);
        if (validated == NULL) {
            return -1;
        }
        self->is_safe = validated;  // reuse reference
    }
    else {
        self->is_safe = Py_NewRef(state->safe_uuid_unknown);
    }

    return 0;
}


static const uint8_t INT_TO_HEX[] = "0123456789abcdef";

// Lookup table for hex character to value conversion
// -1 for invalid characters
static const int8_t HEX_TO_INT[256] = {
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

static inline void
byte_to_hex(uint8_t byte, char *hex)
{
    hex[0] = INT_TO_HEX[(byte >> 4) & 0xf];
    hex[1] = INT_TO_HEX[byte & 0xf];
}

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

        part = HEX_TO_INT[ch];
        if (part == -1) {
            PyErr_SetString(
                PyExc_ValueError,
                "badly formed hexadecimal UUID string"
            );
            return -1;
        }

        if (acc_set) {
            acc |= (uint8_t)part;
            self->bytes[j] = acc;
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

static int
from_int(uuidobject *self, PyObject *int_value)
{
    // Convert a 128-bit integer to UUID bytes (big-endian)
    // Check that the integer is in valid range (0 to 2^128 - 1)

    uuid_state *state = get_uuid_state_by_cls(Py_TYPE(self));

    // Check if it's less than min (0)
    int cmp = PyLong_IsNegative(int_value);
    if (cmp < 0) {
        return -1;
    }
    if (cmp == 1) {
        PyErr_SetString(PyExc_ValueError,
            "int is out of range (need a 128-bit value)");
        return -1;
    }

    // Check if it's greater than max (2^128 - 1)
    cmp = PyObject_RichCompareBool(int_value, state->uint128_max, Py_GT);
    if (cmp < 0) {
        return -1;
    }
    if (cmp == 1) {
        PyErr_SetString(PyExc_ValueError,
            "int is out of range (need a 128-bit value)");
        return -1;
    }

    // Convert to bytes (big-endian)
    if (_PyLong_AsByteArray(
            (PyLongObject *)int_value,
            (unsigned char *)self->bytes,
            16,
            0,  // big-endian
            0,  // unsigned
            1   // with_exceptions
        ) < 0)
    {
        return -1;
    }

    // Cache the int value since we already have it
    self->cached_int = Py_NewRef(int_value);

    return 0;
}

static int
from_fields(uuidobject *self, PyObject *fields)
{
    // Call uuid._from_fields() to get the int value
    uuid_state *state = get_uuid_state_by_cls(Py_TYPE(self));

    PyObject *int_value = PyObject_CallOneArg(state->from_fields_func, fields);
    if (int_value == NULL) {
        return -1;
    }

    // Convert the int to bytes using our existing from_int function
    // Note: from_int will cache the int_value for us
    int result = from_int(self, int_value);
    Py_DECREF(int_value);

    return result;
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

static PyObject *
Uuid_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    uuidobject *self;
    self = (uuidobject *)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    self->cached_int = NULL;
    self->is_safe = NULL;
    self->weakreflist = NULL;
    memset(self->bytes, 0, 16);
    self->cached_hash = -1;

    return (PyObject *)self;
}

static void
Uuid_dealloc(PyObject *obj)
{
    uuidobject *uuid = (uuidobject *)obj;
    if (uuid->weakreflist != NULL) {
        PyObject_ClearWeakRefs(obj);
    }
    Py_XDECREF(uuid->cached_int);
    Py_XDECREF(uuid->is_safe);
    PyObject_Free(uuid);
}


static PyObject *
Uuid_get_int(uuidobject *self, void *closure)
{
    return get_int(self);
}

static PyObject *
Uuid_get_is_safe(uuidobject *self, void *closure)
{
    if (self->is_safe == NULL) {
        Py_RETURN_NONE;
    }
    return Py_NewRef(self->is_safe);
}

static PyObject *
Uuid_get_hex(uuidobject *self, void *closure)
{
    // Convert 16 bytes to 32 hex characters
    char hex[32];
    for (int i = 0; i < 16; i++) {
        byte_to_hex(self->bytes[i], &hex[i * 2]);
    }

    // Return as a Python string
    return PyUnicode_FromStringAndSize(hex, 32);
}

static PyObject *
Uuid_get_variant(uuidobject *self, void *closure)
{
    // Get module state
    uuid_state *state = get_uuid_state_by_cls(Py_TYPE(self));

    uint8_t variant_byte = self->bytes[8];

    // xxx - three high bits of variant_byte are unknown
    if (!(variant_byte & 0x80)) {   // & 0b1000_0000
        // 0xx - RESERVED_NCS
        return Py_NewRef(state->reserved_ncs);
    }

    // 1xx -- we know that high bit must be 1
    if (!(variant_byte & 0x40)) {   // & 0b0100_0000
        // 10x - RFC_4122
        return Py_NewRef(state->rfc_4122);
    }

    // 11x -- we know that two high bits are 1
    if (!(variant_byte & 0x20)) {   // & 0b0010_0000
        // 110 - RESERVED_MICROSOFT
        return Py_NewRef(state->reserved_microsoft);
    }

    // 111 -- we know that all three high bits are 1 - RESERVED_FUTURE
    return Py_NewRef(state->reserved_future);
}

static long
get_version(uuidobject *self)
{
    // RFC_4122 is when bit 7 is set (0x80) and bit 6 is not set (0x40)
    // 0xc0 = 0b11000000
    // 0x80 = 0b10000000
    if ((self->bytes[8] & 0xc0) != 0x80) {
        return -1;
    }
    return (self->bytes[6] >> 4) & 0xf;
}

static PyObject *
Uuid_get_version(uuidobject *self, void *closure)
{
    long ver = get_version(self);
    if (ver == -1) {
        Py_RETURN_NONE;
    }
    return PyLong_FromLong(ver);
}

static PyObject *
Uuid_get_time_low(uuidobject *self, void *closure)
{
    // Bytes 0-3 (32 bits) in big-endian
    uint32_t time_low = ((uint32_t)self->bytes[0] << 24) |
                        ((uint32_t)self->bytes[1] << 16) |
                        ((uint32_t)self->bytes[2] << 8) |
                        ((uint32_t)self->bytes[3]);
    return PyLong_FromUnsignedLong(time_low);
}

static PyObject *
Uuid_get_time_mid(uuidobject *self, void *closure)
{
    // Bytes 4-5 (16 bits) in big-endian
    uint16_t time_mid = ((uint16_t)self->bytes[4] << 8) |
                        ((uint16_t)self->bytes[5]);
    return PyLong_FromUnsignedLong(time_mid);
}

static PyObject *
Uuid_get_time_hi_version(uuidobject *self, void *closure)
{
    // Bytes 6-7 (16 bits) in big-endian
    uint16_t time_hi_version = ((uint16_t)self->bytes[6] << 8) |
                                ((uint16_t)self->bytes[7]);
    return PyLong_FromUnsignedLong(time_hi_version);
}

static PyObject *
Uuid_get_clock_seq_hi_variant(uuidobject *self, void *closure)
{
    // Byte 8 (8 bits)
    return PyLong_FromUnsignedLong(self->bytes[8]);
}

static PyObject *
Uuid_get_clock_seq_low(uuidobject *self, void *closure)
{
    // Byte 9 (8 bits)
    return PyLong_FromUnsignedLong(self->bytes[9]);
}

static PyObject *
Uuid_nb_int(PyObject *self)
{
    return get_int((uuidobject *)self);
}

static PyObject *
Uuid_str(PyObject *self)
{
    uuidobject *uuid = (uuidobject *)self;

    // UUID string format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars)
    char str[36];

    // Convert bytes to hex with hyphens at the right positions
    // Bytes 0-3 (8 hex chars)
    for (int i = 0; i < 4; i++) {
        byte_to_hex(uuid->bytes[i], &str[i * 2]);
    }
    str[8] = '-';

    // Bytes 4-5 (4 hex chars)
    for (int i = 4; i < 6; i++) {
        byte_to_hex(uuid->bytes[i], &str[9 + (i - 4) * 2]);
    }
    str[13] = '-';

    // Bytes 6-7 (4 hex chars)
    for (int i = 6; i < 8; i++) {
        byte_to_hex(uuid->bytes[i], &str[14 + (i - 6) * 2]);
    }
    str[18] = '-';

    // Bytes 8-9 (4 hex chars)
    for (int i = 8; i < 10; i++) {
        byte_to_hex(uuid->bytes[i], &str[19 + (i - 8) * 2]);
    }
    str[23] = '-';

    // Bytes 10-15 (12 hex chars)
    for (int i = 10; i < 16; i++) {
        byte_to_hex(uuid->bytes[i], &str[24 + (i - 10) * 2]);
    }

    return PyUnicode_FromStringAndSize(str, 36);
}

static PyObject *
Uuid_repr(PyObject *self)
{
    // Get the string representation
    PyObject *str_obj = Uuid_str(self);
    if (str_obj == NULL) {
        return NULL;
    }

    // Get the class name (can't use tp_name -- we don't need full name)
    PyObject *cls_name = PyObject_GetAttrString((PyObject *)Py_TYPE(self), "__name__");
    if (cls_name == NULL) {
        Py_DECREF(str_obj);
        return NULL;
    }

    // Format as "ClassName('...')" matching Python's '%s(%r)' % (self.__class__.__name__, str(self))
    PyObject *repr = PyUnicode_FromFormat("%U('%U')", cls_name, str_obj);
    Py_DECREF(str_obj);
    Py_DECREF(cls_name);
    return repr;
}

static PyObject *
Uuid_get_urn(uuidobject *self, void *closure)
{
    // Get the string representation
    PyObject *str_obj = Uuid_str((PyObject *)self);
    if (str_obj == NULL) {
        return NULL;
    }

    // Prepend "urn:uuid:"
    PyObject *urn = PyUnicode_FromFormat("urn:uuid:%U", str_obj);
    Py_DECREF(str_obj);
    return urn;
}

static Py_hash_t
Uuid_hash(PyObject *self)
{
    uuidobject *uuid = (uuidobject *)self;
    if (uuid->cached_hash != -1) {
        // UUIDs are very often used in dicts/sets, makes
        // sense to cache the index value to make hashing
        // as fast as possible.
        return uuid->cached_hash;
    }

    PyObject *int_value = get_int(uuid);
    Py_hash_t hash = PyObject_Hash(int_value);
    Py_DECREF(int_value);

    if (hash == -1) {
        return -1;
    }

    uuid->cached_hash = hash;
    return hash;

}

static PyGetSetDef Uuid_getset[] = {
    {"int", (getter)Uuid_get_int, NULL, "UUID as a 128-bit integer", NULL},
    {"is_safe", (getter)Uuid_get_is_safe, NULL, "UUID safety status", NULL},
    {"hex", (getter)Uuid_get_hex, NULL, "UUID as a 32-character hex string", NULL},
    {"urn", (getter)Uuid_get_urn, NULL, "UUID as a URN", NULL},
    {"variant", (getter)Uuid_get_variant, NULL, "UUID variant", NULL},
    {"version", (getter)Uuid_get_version, NULL, "UUID version", NULL},
    {"time_low", (getter)Uuid_get_time_low, NULL, "Time low field (32 bits)", NULL},
    {"time_mid", (getter)Uuid_get_time_mid, NULL, "Time mid field (16 bits)", NULL},
    {"time_hi_version", (getter)Uuid_get_time_hi_version, NULL, "Time high and version field (16 bits)", NULL},
    {"clock_seq_hi_variant", (getter)Uuid_get_clock_seq_hi_variant, NULL, "Clock sequence high and variant field (8 bits)", NULL},
    {"clock_seq_low", (getter)Uuid_get_clock_seq_low, NULL, "Clock sequence low field (8 bits)", NULL},
    {NULL}  /* Sentinel */
};

static PyMemberDef Uuid_members[] = {
    {"__weaklistoffset__", Py_T_PYSSIZET, offsetof(uuidobject, weakreflist), Py_READONLY},
    {NULL}  /* Sentinel */
};

static PyType_Slot Uuid_slots[] = {
    {Py_tp_new, Uuid_new},
    {Py_tp_dealloc, Uuid_dealloc},
    {Py_tp_getattro, PyObject_GenericGetAttr},
    {Py_tp_getset, Uuid_getset},
    {Py_tp_members, Uuid_members},
    {Py_tp_init, _uuid_UUIDBase___init__},
    {Py_tp_doc, (void *)_uuid_UUIDBase___init____doc__},
    {Py_tp_str, Uuid_str},
    {Py_tp_repr, Uuid_repr},
    {Py_tp_hash, Uuid_hash},
    {Py_nb_int, Uuid_nb_int},
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
    Py_VISIT(state->safe_uuid);
    Py_VISIT(state->safe_uuid_safe);
    Py_VISIT(state->safe_uuid_unsafe);
    Py_VISIT(state->safe_uuid_unknown);
    Py_VISIT(state->uint128_max);
    Py_VISIT(state->from_fields_func);
    Py_VISIT(state->reserved_ncs);
    Py_VISIT(state->rfc_4122);
    Py_VISIT(state->reserved_microsoft);
    Py_VISIT(state->reserved_future);
    return 0;
}

static int
module_clear(PyObject *mod)
{
    uuid_state *state = get_uuid_state(mod);
    Py_CLEAR(state->UuidType);
    Py_CLEAR(state->safe_uuid);
    Py_CLEAR(state->safe_uuid_safe);
    Py_CLEAR(state->safe_uuid_unsafe);
    Py_CLEAR(state->safe_uuid_unknown);
    Py_CLEAR(state->uint128_max);
    Py_CLEAR(state->from_fields_func);
    Py_CLEAR(state->reserved_ncs);
    Py_CLEAR(state->rfc_4122);
    Py_CLEAR(state->reserved_microsoft);
    Py_CLEAR(state->reserved_future);
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
    safe_uuid = state->safe_uuid =PyObject_GetAttrString(uuid_mod, "SafeUUID");
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

    // Import _UINT_128_MAX and _UINT_128_MIN from uuid module
    state->uint128_max = PyObject_GetAttrString(uuid_mod, "_UINT_128_MAX");
    if (state->uint128_max == NULL) {
        goto fail;
    }

    // Import _from_fields function from uuid module
    state->from_fields_func = PyObject_GetAttrString(uuid_mod, "_from_fields");
    if (state->from_fields_func == NULL) {
        goto fail;
    }

    // Import variant constants from uuid module
    state->reserved_ncs = PyObject_GetAttrString(uuid_mod, "RESERVED_NCS");
    if (state->reserved_ncs == NULL) {
        goto fail;
    }
    state->rfc_4122 = PyObject_GetAttrString(uuid_mod, "RFC_4122");
    if (state->rfc_4122 == NULL) {
        goto fail;
    }
    state->reserved_microsoft = PyObject_GetAttrString(uuid_mod, "RESERVED_MICROSOFT");
    if (state->reserved_microsoft == NULL) {
        goto fail;
    }
    state->reserved_future = PyObject_GetAttrString(uuid_mod, "RESERVED_FUTURE");
    if (state->reserved_future == NULL) {
        goto fail;
    }

    Py_CLEAR(uuid_mod);
    return 0;

fail:
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
