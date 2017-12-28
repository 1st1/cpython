#include "Python.h"

#include "structmember.h"
#include "internal/pystate.h"
#include "internal/context.h"


typedef enum {F_ERROR, F_NOT_FOUND, F_FOUND} hamt_find_t;
typedef enum {W_ERROR, W_NOT_FOUND, W_EMPTY, W_NEWNODE} hamt_without_t;
typedef enum {I_ITEM, I_END} hamt_iter_t;


static PyHamtObject *
hamt_new(void);

static PyHamtObject *
hamt_assoc(PyHamtObject *o, PyObject *key, PyObject *val);

static PyHamtObject *
hamt_without(PyHamtObject *o, PyObject *key);

static hamt_find_t
hamt_find(PyHamtObject *o, PyObject *key, PyObject **val);

static int
hamt_eq(PyHamtObject *v, PyHamtObject *w);

static Py_ssize_t
hamt_len(PyHamtObject *o);

static int
hamt_contains(PyHamtObject *self, PyObject *key);

static PyObject *
hamt_subscript(PyHamtObject *self, PyObject *key);

static PyObject *
hamt_get(PyHamtObject *self, PyObject *key, PyObject *def);

static PyObject *
hamt_iter_new_keys(PyHamtObject *o);

static PyObject *
hamt_iter_new_values(PyHamtObject *o);

static PyObject *
hamt_iter_new_items(PyHamtObject *o);


#include "clinic/context.c.h"
/*[clinic input]
module _contextvars
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=a0955718c8b8cea6]*/


/////////////////////////// Context API


static PyContext *
context_new(PyHamtObject *vars);

static PyContextToken *
token_new(PyContextVar *var, PyObject *val);

static PyContextVar *
contextvar_new(PyObject *name, PyObject *def);

static int
contextvar_set(PyContextVar *var, PyObject *val);

static int
contextvar_del(PyContextVar *var);


PyObject *
_PyContext_NewHamtForTests(void)
{
    return (PyObject *)hamt_new();
}


PyContext *
PyContext_New(void)
{
    return context_new(NULL);
}


PyContext *
PyContext_Get(void)
{
    PyThreadState *ts = PyThreadState_Get();
    return context_new((PyHamtObject*)ts->contextvars);
}


PyContextVar *
PyContextVar_New(PyObject *name, PyObject *def)
{
    return contextvar_new(name, def);
}


PyObject *
PyContextVar_Get(PyContextVar *var, PyObject *def)
{
    if (!PyContextVar_CheckExact(var)) {
        PyErr_SetString(
            PyExc_TypeError, "an instance of ContextVar was expected");
        return NULL;
    }

    PyThreadState *ts = PyThreadState_Get();
    if (ts->contextvars == NULL) {
        goto not_found;
    }

    assert(PyHamt_Check(ts->contextvars));
    PyHamtObject *vars = (PyHamtObject *)ts->contextvars;

    PyObject *val = NULL;
    hamt_find_t res = hamt_find(vars, (PyObject*)var, &val);
    switch (res) {
        case F_ERROR:
            return NULL;
        case F_FOUND:
            Py_INCREF(val);
            return val;
        case F_NOT_FOUND:
            goto not_found;
    }

not_found:
    if (def == NULL) {
        if (var->var_default != NULL) {
            Py_INCREF(var->var_default);
            return var->var_default;
        }

        PyErr_SetObject(PyExc_LookupError, (PyObject*) var);
        return NULL;
    }
    else {
        Py_INCREF(def);
        return def;
    }
}


PyContextToken *
PyContextVar_Set(PyContextVar *var, PyObject *val)
{
    if (!PyContextVar_CheckExact(var)) {
        PyErr_SetString(
            PyExc_TypeError, "an instance of ContextVar was expected");
        return NULL;
    }

    PyThreadState *ts = PyThreadState_Get();

    if (ts->contextvars == NULL) {
        ts->contextvars = (PyObject *)hamt_new();
        if (ts->contextvars == NULL) {
            return NULL;
        }
    }

    assert(PyHamt_Check(ts->contextvars));
    PyHamtObject *vars = (PyHamtObject *)ts->contextvars;

    PyObject *old_val = NULL;
    hamt_find_t found = hamt_find(vars, (PyObject *)var, &old_val);
    if (found == F_ERROR) {
        return NULL;
    }

    Py_XINCREF(old_val);
    PyContextToken *tok = token_new(var, old_val);
    Py_XDECREF(old_val);

    if (contextvar_set(var, val)) {
        Py_DECREF(tok);
        return NULL;
    }

    return tok;
}


int
PyContextVar_Reset(PyContextVar *var, PyContextToken *tok)
{
    if (var != tok->tok_var) {
        PyErr_SetString(PyExc_ValueError,
                        "Token was created by a different ContextVar");
        return -1;
    }

    if (tok->tok_used) {
        return 0;
    }
    tok->tok_used = 1;

    if (tok->tok_oldval == NULL) {
        return contextvar_del(var);
    }
    else {
        return contextvar_set(var, tok->tok_oldval);
    }
}


/////////////////////////// PyContext

/*[clinic input]
class _contextvars.Context "PyContext *" "&PyContext_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=bdf87f8e0cb580e8]*/

static PyContext *
context_new(PyHamtObject *vars)
{
    PyContext *ctx = PyObject_GC_New(PyContext, &PyContext_Type);
    if (ctx == NULL) {
        return NULL;
    }

    if (vars == NULL) {
        ctx->ctx_vars = hamt_new();
        if (ctx->ctx_vars == NULL) {
            Py_DECREF(ctx);
            return NULL;
        }
    }
    else {
        assert(PyHamt_Check(vars));
        Py_INCREF(vars);
        ctx->ctx_vars = vars;
    }

    ctx->ctx_weakreflist = NULL;
    PyObject_GC_Track(ctx);
    return ctx;
}

static int
context_check_key_type(PyObject *key)
{
    if (!PyContextVar_CheckExact(key)) {
        // abort();
        PyErr_Format(PyExc_TypeError,
                     "a ContextVar key was expected, got %R", key);
        return -1;
    }
    return 0;
}

static PyObject *
context_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (PyTuple_Size(args) || (kwds != NULL && PyDict_Size(kwds))) {
        PyErr_SetString(
            PyExc_TypeError, "Context() does not accept any arguments");
        return NULL;
    }
    return (PyObject *)PyContext_New();
}

static int
context_tp_clear(PyContext *self)
{
    Py_CLEAR(self->ctx_vars);
    return 0;
}

static int
context_tp_traverse(PyContext *self, visitproc visit, void *arg)
{
    Py_VISIT(self->ctx_vars);
    return 0;
}

static void
context_tp_dealloc(PyContext *self)
{
    PyObject_GC_UnTrack(self);
    if (self->ctx_weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*)self);
    }
    (void)context_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
context_tp_iter(PyContext *self)
{
    return hamt_iter_new_keys(self->ctx_vars);
}

static PyObject *
context_tp_richcompare(PyObject *v, PyObject *w, int op)
{
    if (!PyContext_CheckExact(v) || !PyContext_CheckExact(w) ||
            (op != Py_EQ && op != Py_NE))
    {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int res = hamt_eq(((PyContext *)v)->ctx_vars, ((PyContext *)w)->ctx_vars);
    if (res < 0) {
        return NULL;
    }

    if (op == Py_NE) {
        res = !res;
    }

    if (res) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

static Py_ssize_t
context_tp_len(PyContext *self)
{
    return hamt_len(self->ctx_vars);
}

static PyObject *
context_tp_subscript(PyContext *self, PyObject *key)
{
    if (context_check_key_type(key)) {
        return NULL;
    }
    return hamt_subscript(self->ctx_vars, key);
}

static int
context_tp_contains(PyContext *self, PyObject *key)
{
    if (context_check_key_type(key)) {
        return -1;
    }
    return hamt_contains(self->ctx_vars, key);
}


/*[clinic input]
_contextvars.Context.get
    key: object
    default: object = None
    /
[clinic start generated code]*/

static PyObject *
_contextvars_Context_get_impl(PyContext *self, PyObject *key,
                              PyObject *default_value)
/*[clinic end generated code: output=0c54aa7664268189 input=8d4c33c8ecd6d769]*/
{
    if (context_check_key_type(key)) {
        return NULL;
    }
    return hamt_get(self->ctx_vars, key, default_value);
}


/*[clinic input]
_contextvars.Context.items
[clinic start generated code]*/

static PyObject *
_contextvars_Context_items_impl(PyContext *self)
/*[clinic end generated code: output=fa1655c8a08502af input=2d570d1455004979]*/
{
    return hamt_iter_new_items(self->ctx_vars);
}


/*[clinic input]
_contextvars.Context.keys
[clinic start generated code]*/

static PyObject *
_contextvars_Context_keys_impl(PyContext *self)
/*[clinic end generated code: output=177227c6b63ec0e2 input=13005e142fbbf37d]*/
{
    return hamt_iter_new_keys(self->ctx_vars);
}


/*[clinic input]
_contextvars.Context.values
[clinic start generated code]*/

static PyObject *
_contextvars_Context_values_impl(PyContext *self)
/*[clinic end generated code: output=d286dabfc8db6dde input=c2cbc40a4470e905]*/
{
    return hamt_iter_new_keys(self->ctx_vars);
}


static PyObject *
context_run(PyContext *self, PyObject *const *args,
            Py_ssize_t nargs, PyObject *kwnames)
{
    PyThreadState *ts = PyThreadState_Get();

    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError,
                        "run() missing 1 required positional argument");
        return NULL;
    }

    PyObject *old_ctx = ts->contextvars;

    Py_INCREF(self->ctx_vars);
    ts->contextvars = (PyObject *)self->ctx_vars;

    PyObject *call_result = _PyObject_FastCallKeywords(
        args[0], args + 1, nargs - 1, kwnames);

    Py_DECREF(self->ctx_vars);
    self->ctx_vars = (PyHamtObject *)ts->contextvars;
    ts->contextvars = old_ctx;

    return call_result;
}


static PyMethodDef PyContext_methods[] = {
    _CONTEXTVARS_CONTEXT_GET_METHODDEF
    _CONTEXTVARS_CONTEXT_ITEMS_METHODDEF
    _CONTEXTVARS_CONTEXT_KEYS_METHODDEF
    _CONTEXTVARS_CONTEXT_VALUES_METHODDEF
    {"run", (PyCFunction)context_run, METH_FASTCALL | METH_KEYWORDS, NULL},
    {NULL, NULL}
};

static PySequenceMethods PyContext_as_sequence = {
    0,                                   /* sq_length */
    0,                                   /* sq_concat */
    0,                                   /* sq_repeat */
    0,                                   /* sq_item */
    0,                                   /* sq_slice */
    0,                                   /* sq_ass_item */
    0,                                   /* sq_ass_slice */
    (objobjproc)context_tp_contains,     /* sq_contains */
    0,                                   /* sq_inplace_concat */
    0,                                   /* sq_inplace_repeat */
};

static PyMappingMethods PyContext_as_mapping = {
    (lenfunc)context_tp_len,             /* mp_length */
    (binaryfunc)context_tp_subscript,    /* mp_subscript */
};

PyTypeObject PyContext_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "Context",
    sizeof(PyContext),
    .tp_methods = PyContext_methods,
    .tp_as_mapping = &PyContext_as_mapping,
    .tp_as_sequence = &PyContext_as_sequence,
    .tp_iter = (getiterfunc)context_tp_iter,
    .tp_dealloc = (destructor)context_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_richcompare = context_tp_richcompare,
    .tp_traverse = (traverseproc)context_tp_traverse,
    .tp_clear = (inquiry)context_tp_clear,
    .tp_new = context_tp_new,
    .tp_weaklistoffset = offsetof(PyContext, ctx_weakreflist),
    .tp_hash = PyObject_HashNotImplemented,
};


/////////////////////////// ContextVar


static int
contextvar_set(PyContextVar *var, PyObject *val)
{
    PyThreadState *ts = PyThreadState_Get();

    if (ts->contextvars == NULL) {
        ts->contextvars = (PyObject *)hamt_new();
        if (ts->contextvars == NULL) {
            return -1;
        }
    }

    assert(PyHamt_Check(ts->contextvars));
    PyHamtObject *vars = (PyHamtObject *)ts->contextvars;

    PyHamtObject *new_vars = hamt_assoc(vars, (PyObject *)var, val);
    if (new_vars == NULL) {
        return -1;
    }

    ts->contextvars = (PyObject *)new_vars;
    Py_DECREF(vars);
    return 0;
}

static int
contextvar_del(PyContextVar *var)
{
    PyThreadState *ts = PyThreadState_Get();

    if (ts->contextvars == NULL) {
        return 0;
    }

    assert(PyHamt_Check(ts->contextvars));
    PyHamtObject *vars = (PyHamtObject *)ts->contextvars;

    PyHamtObject *new_vars = hamt_without(vars, (PyObject *)var);
    if (new_vars == NULL) {
        return -1;
    }

    if (vars == new_vars) {
        Py_DECREF(new_vars);
        PyErr_SetObject(PyExc_LookupError, (PyObject *)var);
        return -1;
    }

    ts->contextvars = (PyObject *)new_vars;
    Py_DECREF(vars);
    return 0;
}

static Py_hash_t
contextvar_generate_hash(void *addr, PyObject *name)
{
    /* Take hash of the name and XOR it with the default object hash.
       We do this to ensure that even sequentially allocated ContextVar
       objects have drastically different hashes.

       OTOH, we can't just return the hash of name.  For two variables
       below:

            c1 = ContextVar('somename')
            c2 = ContextVar('somename')

       c1 and c2 are completely different variables that happen to
       have the same name.
    */

    Py_hash_t name_hash = PyObject_Hash(name);
    if (name_hash == -1) {
        return -1;
    }

    Py_hash_t res = _Py_HashPointer(addr) ^ name_hash;
    return res == -1 ? -2 : res;
}

static PyContextVar *
contextvar_new(PyObject *name, PyObject *def)
{
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError,
                        "context variable name must be a str");
        return NULL;
    }

    PyContextVar *var = PyObject_GC_New(PyContextVar, &PyContextVar_Type);
    if (var == NULL) {
        return NULL;
    }

    var->var_hash = contextvar_generate_hash(var, name);
    if (var->var_hash == -1) {
        Py_DECREF(var);
        return NULL;
    }

    Py_INCREF(name);
    var->var_name = name;

    Py_XINCREF(def);
    var->var_default = def;

    PyObject_GC_Track(var);
    return var;
}


/*[clinic input]
class _contextvars.ContextVar "PyContextVar *" "&PyContextVar_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=445da935fa8883c3]*/


static PyObject *
contextvar_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "default", NULL};
    PyObject *name;
    PyObject *def = NULL;

    if (!PyArg_ParseTupleAndKeywords(
            args, kwds, "O|$O:ContextVar", kwlist, &name, &def))
    {
        return NULL;
    }

    return (PyObject *)contextvar_new(name, def);
}

static int
contextvar_tp_clear(PyContextVar *self)
{
    Py_CLEAR(self->var_name);
    Py_CLEAR(self->var_default);
    return 0;
}

static int
contextvar_tp_traverse(PyContextVar *self, visitproc visit, void *arg)
{
    Py_VISIT(self->var_name);
    Py_VISIT(self->var_default);
    return 0;
}

static void
contextvar_tp_dealloc(PyContextVar *self)
{
    PyObject_GC_UnTrack(self);
    (void)contextvar_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static Py_hash_t
contextvar_tp_hash(PyContextVar *self)
{
    return self->var_hash;
}

static PyObject *
contextvar_tp_repr(PyContextVar *self)
{
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);
    writer.min_length = 33;  /* len of "<ContextVar name= at 0x10b0cd0f0>" */

    if (_PyUnicodeWriter_WriteASCIIString(
            &writer, "<ContextVar name=", 17) < 0)
    {
        goto error;
    }

    PyObject *name = PyObject_Repr(self->var_name);
    if (name == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, name) < 0) {
        Py_DECREF(name);
        goto error;
    }
    Py_DECREF(name);

    if (self->var_default != NULL) {
        if (_PyUnicodeWriter_WriteASCIIString(&writer, " default=", 9) < 0) {
            goto error;
        }

        PyObject *def = PyObject_Repr(self->var_default);
        if (def == NULL) {
            goto error;
        }
        if (_PyUnicodeWriter_WriteStr(&writer, def) < 0) {
            Py_DECREF(def);
            goto error;
        }
        Py_DECREF(def);
    }

    PyObject *addr = PyUnicode_FromFormat(" at %p>", self);
    if (addr == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, addr) < 0) {
        Py_DECREF(addr);
        goto error;
    }
    Py_DECREF(addr);

    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}


/*[clinic input]
_contextvars.ContextVar.get
    default: object = NULL
    /
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_get_impl(PyContextVar *self, PyObject *default_value)
/*[clinic end generated code: output=0746bd0aa2ced7bf input=8d002b02eebbb247]*/
{
    return PyContextVar_Get(self, default_value);
}

/*[clinic input]
_contextvars.ContextVar.set
    value: object
    /
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_set(PyContextVar *self, PyObject *value)
/*[clinic end generated code: output=446ed5e820d6d60b input=a2d88f57c6d86f7c]*/
{
    return (PyObject *)PyContextVar_Set(self, value);
}

/*[clinic input]
_contextvars.ContextVar.reset
    token: object
    /
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_reset(PyContextVar *self, PyObject *token)
/*[clinic end generated code: output=d4ee34d0742d62ee input=4c871b6f1f31a65f]*/
{
    if (!PyContextToken_CheckExact(token)) {
        PyErr_Format(PyExc_TypeError,
                     "expected an instance of Token, got %R", token);
        return NULL;
    }

    if (PyContextVar_Reset(self, (PyContextToken *)token)) {
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyMethodDef PyContextVar_methods[] = {
    _CONTEXTVARS_CONTEXTVAR_GET_METHODDEF
    _CONTEXTVARS_CONTEXTVAR_SET_METHODDEF
    _CONTEXTVARS_CONTEXTVAR_RESET_METHODDEF
    {NULL, NULL}
};

PyTypeObject PyContextVar_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "ContextVar",
    sizeof(PyContextVar),
    .tp_methods = PyContextVar_methods,
    .tp_dealloc = (destructor)contextvar_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)contextvar_tp_traverse,
    .tp_clear = (inquiry)contextvar_tp_clear,
    .tp_new = contextvar_tp_new,
    .tp_free = PyObject_GC_Del,
    .tp_hash = (hashfunc)contextvar_tp_hash,
    .tp_repr = (reprfunc)contextvar_tp_repr,
};


/////////////////////////// Token


/*[clinic input]
class _contextvars.Token "PyContextToken *" "&PyContextToken_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=338a5e2db13d3f5b]*/


static PyObject *
token_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_RuntimeError,
                    "Tokens can only be created by ContextVars");
    return NULL;
}

static int
token_tp_clear(PyContextToken *self)
{
    Py_CLEAR(self->tok_var);
    Py_CLEAR(self->tok_oldval);
    return 0;
}

static int
token_tp_traverse(PyContextToken *self, visitproc visit, void *arg)
{
    Py_VISIT(self->tok_var);
    Py_VISIT(self->tok_oldval);
    return 0;
}

static void
token_tp_dealloc(PyContextToken *self)
{
    PyObject_GC_UnTrack(self);
    (void)token_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static PyMethodDef PyContextToken_methods[] = {
    {NULL, NULL}
};

PyTypeObject PyContextToken_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "Token",
    sizeof(PyContextToken),
    .tp_methods = PyContextToken_methods,
    .tp_dealloc = (destructor)token_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)token_tp_traverse,
    .tp_clear = (inquiry)token_tp_clear,
    .tp_new = token_tp_new,
    .tp_free = PyObject_GC_Del,
    .tp_hash = PyObject_HashNotImplemented,
};

static PyContextToken *
token_new(PyContextVar *var, PyObject *val)
{
    PyContextToken *tok = PyObject_GC_New(PyContextToken, &PyContextToken_Type);
    if (tok == NULL) {
        return NULL;
    }

    Py_INCREF(var);
    tok->tok_var = var;

    Py_XINCREF(val);
    tok->tok_oldval = val;

    tok->tok_used = 0;

    PyObject_GC_Track(tok);
    return tok;
}


/////////////////////////// HAMT DATASTRUCTURE IMPLEMENTATION


#define IS_ARRAY_NODE(node)     (Py_TYPE(node) == &_PyHamt_ArrayNode_Type)
#define IS_BITMAP_NODE(node)    (Py_TYPE(node) == &_PyHamt_BitmapNode_Type)
#define IS_COLLISION_NODE(node) (Py_TYPE(node) == &_PyHamt_CollisionNode_Type)


#ifndef PyHamtNode_Bitmap_MAXSAVESIZE
#define PyHamtNode_Bitmap_MAXSAVESIZE 20
#endif
#ifndef PyHamtNode_Bitmap_MAXFREELIST
#define PyHamtNode_Bitmap_MAXFREELIST 5
#endif


#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
static PyHamtNode_Bitmap *free_bitmap_list[PyHamtNode_Bitmap_MAXSAVESIZE];
static int numfree_bitmap[PyHamtNode_Bitmap_MAXSAVESIZE];
#endif


#ifndef PyHamtNode_Array_MAXFREELIST
#define PyHamtNode_Array_MAXFREELIST 20
#endif


#if PyHamtNode_Array_MAXFREELIST > 0
static PyHamtNode_Array *free_array_list;
static int numfree_array;
#endif


static PyHamtNode_Bitmap *_empty_bitmap_node;


static PyHamtObject *
hamt_alloc(void);

static PyHamtNode *
hamt_node_assoc(PyHamtNode *node,
                uint32_t shift, int32_t hash,
                PyObject *key, PyObject *val, int* added_leaf);

static hamt_without_t
hamt_node_without(PyHamtNode *node,
                  uint32_t shift, int32_t hash,
                  PyObject *key,
                  PyHamtNode **new_node);

static hamt_find_t
hamt_node_find(PyHamtNode *node,
               uint32_t shift, int32_t hash,
               PyObject *key, PyObject **val);

#ifdef Py_DEBUG
static int
hamt_node_dump(PyHamtNode *node,
               _PyUnicodeWriter *writer, int level);
#endif

static PyHamtNode *
hamt_node_array_new(Py_ssize_t);

static PyHamtNode *
hamt_node_collision_new(int32_t hash, Py_ssize_t size);

static inline Py_ssize_t
hamt_node_collision_count(PyHamtNode_Collision *node);


#ifdef Py_DEBUG
static void
_hamt_node_array_validate(void *o)
{
    assert(IS_ARRAY_NODE(o));
    PyHamtNode_Array *node = (PyHamtNode_Array*)(o);
    Py_ssize_t i = 0, count = 0;
    for (; i < HAMT_ARRAY_NODE_SIZE; i++) {
        if (node->a_array[i] != NULL) {
            count++;
        }
    }
    assert(count == node->a_count);
}

#define VALIDATE_ARRAY_NODE(NODE) \
    do { _hamt_node_array_validate(NODE); } while (0);
#else
#define VALIDATE_ARRAY_NODE(NODE)
#endif


/* Returns -1 on error */
static inline int32_t
hamt_hash(PyObject *o)
{
    Py_hash_t hash = PyObject_Hash(o);

#if SIZEOF_PY_HASH_T <= 4
    return hash;
#else
    if (hash == -1) {
        /* exception */
        return -1;
    }

    /* While it's suboptimal to reduce Python's 64 bit hash to
       32 bits via xor, it seems that the resulting hash function
       is good enough.  This is also how Java hashes its Long type.
       Storing 10, 100, 1000 Python strings results in a relatively
       shallow and uniform tree structure.

       Please don't change this hashing algorithm, as there are many
       tests that test some exact tree shape to cover all code paths.
    */
    int32_t xored = (int32_t)(hash & 0xffffffffl) ^ (int32_t)(hash >> 32);
    return xored == -1 ? -2 : xored;
#endif
}

static inline uint32_t
hamt_mask(int32_t hash, uint32_t shift)
{
    return (((uint32_t)hash >> shift) & 0x01f);
}

static inline uint32_t
hamt_bitpos(int32_t hash, uint32_t shift)
{
    return (uint32_t)1 << hamt_mask(hash, shift);
}

static inline uint32_t
hamt_bitcount(uint32_t i)
{
#if defined(__GNUC__) && (__GNUC__ > 4)
    return (uint32_t)__builtin_popcountl(i);
#elif defined(__clang__) && (__clang_major__ > 3)
    return (uint32_t)__builtin_popcountl(i);
#else
    /* https://graphics.stanford.edu/~seander/bithacks.html */
    i = i - ((i >> 1) & 0x55555555);
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
    return ((i + (i >> 4) & 0xF0F0F0F) * 0x1010101) >> 24;
#endif
}

static inline uint32_t
hamt_bitindex(uint32_t bitmap, uint32_t bit)
{
    return hamt_bitcount(bitmap & (bit - 1));
}


/////////////////////////////////// Dump Helpers
#ifdef Py_DEBUG

static int
_hamt_dump_ident(_PyUnicodeWriter *writer, int level)
{
    /* Write `'    ' * level` to the `writer` */
    PyObject *str = NULL;
    PyObject *num = NULL;
    PyObject *res = NULL;
    int ret = -1;

    str = PyUnicode_FromString("    ");
    if (str == NULL) {
        goto error;
    }

    num = PyLong_FromLong((long)level);
    if (num == NULL) {
        goto error;
    }

    res = PyNumber_Multiply(str, num);
    if (res == NULL) {
        goto error;
    }

    ret = _PyUnicodeWriter_WriteStr(writer, res);

error:
    Py_XDECREF(res);
    Py_XDECREF(str);
    Py_XDECREF(num);
    return ret;
}

static int
_hamt_dump_format(_PyUnicodeWriter *writer, const char *format, ...)
{
    /* A convenient helper combining _PyUnicodeWriter_WriteStr and
       PyUnicode_FromFormatV.
    */
    PyObject* msg;
    int ret;

    va_list vargs;
#ifdef HAVE_STDARG_PROTOTYPES
    va_start(vargs, format);
#else
    va_start(vargs);
#endif
    msg = PyUnicode_FromFormatV(format, vargs);
    va_end(vargs);

    if (msg == NULL) {
        return -1;
    }

    ret = _PyUnicodeWriter_WriteStr(writer, msg);
    Py_DECREF(msg);
    return ret;
}

#endif  /* Py_DEBUG */
/////////////////////////////////// Bitmap Node


static PyHamtNode *
hamt_node_bitmap_new(Py_ssize_t size)
{
    /* Create a new bitmap node of size 'size' */

    PyHamtNode_Bitmap *node;
    Py_ssize_t i;

    assert(size >= 0);
    assert(size % 2 == 0);

    if (size == 0 && _empty_bitmap_node != NULL) {
        Py_INCREF(_empty_bitmap_node);
        return (PyHamtNode *)_empty_bitmap_node;
    }

#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
    /* We have a freelist for bitmap nodes.  Check if we are
       freelisting nodes of this size, and if there's a node
       we can reuse in the freelist.
    */
    if (size > 0 &&
            size < PyHamtNode_Bitmap_MAXSAVESIZE &&
            (node = free_bitmap_list[size]) != NULL)
    {
        free_bitmap_list[size] = (PyHamtNode_Bitmap *)node->b_array[0];
        numfree_bitmap[size]--;
        _Py_NewReference((PyObject *)node);
    }
    else
#endif
    {
        /* No freelist; allocate a new bitmap node */
        node = PyObject_GC_NewVar(
            PyHamtNode_Bitmap, &_PyHamt_BitmapNode_Type, size);
        if (node == NULL) {
            return NULL;
        }

        Py_SIZE(node) = size;
    }

    for (i = 0; i < size; i++) {
        node->b_array[i] = NULL;
    }

    node->b_bitmap = 0;

    _PyObject_GC_TRACK(node);

    if (size == 0 && _empty_bitmap_node == NULL) {
        /* Since bitmap nodes are immutable, we can cache the instance
           for size=0 and reuse it whenever we need an empty bitmap node.
        */
        _empty_bitmap_node = node;
        Py_INCREF(_empty_bitmap_node);
    }

    return (PyHamtNode *)node;
}

static inline Py_ssize_t
hamt_node_bitmap_count(PyHamtNode_Bitmap *node)
{
    return Py_SIZE(node) >> 1;
}

static PyHamtNode_Bitmap *
hamt_node_bitmap_clone(PyHamtNode_Bitmap *node)
{
    /* Clone a bitmap node; return a new one with the same child notes. */

    PyHamtNode_Bitmap *clone;
    Py_ssize_t i;

    clone = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(Py_SIZE(node));
    if (clone == NULL) {
        return NULL;
    }

    for (i = 0; i < Py_SIZE(node); i++) {
        Py_XINCREF(node->b_array[i]);
        clone->b_array[i] = node->b_array[i];
    }

    clone->b_bitmap = node->b_bitmap;
    return clone;
}

static PyHamtNode_Bitmap *
hamt_node_bitmap_clone_without(PyHamtNode_Bitmap *o, uint32_t bit)
{
    assert(bit & o->b_bitmap);
    assert(hamt_node_bitmap_count(o) > 1);

    PyHamtNode_Bitmap *new = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(
        Py_SIZE(o) - 2);
    if (new == NULL) {
        return NULL;
    }

    uint32_t idx = hamt_bitindex(o->b_bitmap, bit);
    uint32_t key_idx = 2 * idx;
    uint32_t val_idx = key_idx + 1;
    uint32_t i;

    for (i = 0; i < key_idx; i++) {
        Py_XINCREF(o->b_array[i]);
        new->b_array[i] = o->b_array[i];
    }

    for (i = val_idx + 1; i < Py_SIZE(o); i++) {
        Py_XINCREF(o->b_array[i]);
        new->b_array[i - 2] = o->b_array[i];
    }

    new->b_bitmap = o->b_bitmap & ~bit;
    return new;
}

static PyHamtNode *
hamt_node_new_bitmap_or_collision(uint32_t shift,
                                  PyObject *key1, PyObject *val1,
                                  int32_t key2_hash,
                                  PyObject *key2, PyObject *val2)
{
    /* Helper method.  Creates a new node for key1/val and key2/val2
       pairs.

       If key1 hash is equal to the hash of key2, a Collision node
       will be created.  If they are not equal, a Bitmap node is
       created.
    */

    int32_t key1_hash = hamt_hash(key1);
    if (key1_hash == -1) {
        return NULL;
    }

    if (key1_hash == key2_hash) {
        PyHamtNode_Collision *n;
        n = (PyHamtNode_Collision *)hamt_node_collision_new(key1_hash, 4);
        if (n == NULL) {
            return NULL;
        }

        Py_INCREF(key1);
        n->c_array[0] = key1;
        Py_INCREF(val1);
        n->c_array[1] = val1;

        Py_INCREF(key2);
        n->c_array[2] = key2;
        Py_INCREF(val2);
        n->c_array[3] = val2;

        return (PyHamtNode *)n;
    }
    else {
        int added_leaf = 0;
        PyHamtNode *n = hamt_node_bitmap_new(0);
        if (n == NULL) {
            return NULL;
        }

        PyHamtNode *n2 = hamt_node_assoc(
            n, shift, key1_hash, key1, val1, &added_leaf);
        Py_DECREF(n);
        if (n2 == NULL) {
            return NULL;
        }

        n = hamt_node_assoc(n2, shift, key2_hash, key2, val2, &added_leaf);
        Py_DECREF(n2);
        if (n == NULL) {
            return NULL;
        }

        return n;
    }
}

static PyHamtNode *
hamt_node_bitmap_assoc(PyHamtNode_Bitmap *self,
                       uint32_t shift, int32_t hash,
                       PyObject *key, PyObject *val, int* added_leaf)
{
    /* assoc operation for bitmap nodes.

       Return: a new node, or self if key/val already is in the
       collection.

       'added_leaf' is later used in 'hamt_assoc' to determine if
       `hamt.set(key, val)` increased the size of the collection.
    */

    uint32_t bit = hamt_bitpos(hash, shift);
    uint32_t idx = hamt_bitindex(self->b_bitmap, bit);

    /* Bitmap node layout:

    +------+------+------+------+  ---  +------+------+
    | key1 | val1 | key2 | val2 |  ...  | keyN | valN |
    +------+------+------+------+  ---  +------+------+
    where `N < Py_SIZE(node)`.

    The `node->b_bitmap` field is a bitmap.  For a given
    `(shift, hash)` pair we can determine:

     - If this node has the corresponding key/val slots.
     - The index of key/val slots.
    */

    if (self->b_bitmap & bit) {
        /* The key is set in this node */

        uint32_t key_idx = 2 * idx;
        uint32_t val_idx = key_idx + 1;

        assert(val_idx < Py_SIZE(self));

        PyObject *key_or_null = self->b_array[key_idx];
        PyObject *val_or_node = self->b_array[val_idx];

        if (key_or_null == NULL) {
            /* key is NULL.  This means that we have a few keys
               that have the same (hash, shift) pair. */

            assert(val_or_node != NULL);

            PyHamtNode *sub_node = hamt_node_assoc(
                (PyHamtNode *)val_or_node,
                shift + 5, hash, key, val, added_leaf);
            if (sub_node == NULL) {
                return NULL;
            }

            if (val_or_node == (PyObject *)sub_node) {
                Py_DECREF(sub_node);
                Py_INCREF(self);
                return (PyHamtNode *)self;
            }

            PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
            if (ret == NULL) {
                return NULL;
            }
            Py_SETREF(ret->b_array[val_idx], (PyObject*)sub_node);
            return (PyHamtNode *)ret;
        }

        assert(key != NULL);
        /* key is not NULL.  This means that we have only one other
           key in this collection that matches our hash for this shift. */

        int comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
        if (comp_err < 0) {  /* exception in __eq__ */
            return NULL;
        }
        if (comp_err == 1) {  /* key == key_or_null */
            comp_err = PyObject_RichCompareBool(val, val_or_node, Py_EQ);
            if (comp_err < 0) {
                return NULL;
            }
            if (comp_err == 1) {
                /* val == val_or_null: we already have the same key/val
                   pair; return self. */
                Py_INCREF(self);
                return (PyHamtNode *)self;
            }

            /* We're setting a new value for the key we had before.
               Make a new bitmap node with a replaced value, and return it. */
            PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
            if (ret == NULL) {
                return NULL;
            }
            Py_INCREF(val);
            Py_SETREF(ret->b_array[val_idx], val);
            return (PyHamtNode *)ret;
        }

        /* It's a new key, and it has the same index as *one* another key.
           We have a collision.  We need to create a new node which will
           combine the existing key and the key we're adding.

           `hamt_node_new_bitmap_or_collision` will either create a new
           Collision node if the keys have identical hashes, or
           a new Bitmap node.
        */
        PyHamtNode *sub_node = hamt_node_new_bitmap_or_collision(
            shift + 5,
            key_or_null, val_or_node,  /* existing key/val */
            hash,
            key, val  /* new key/val */
        );
        if (sub_node == NULL) {
            return NULL;
        }

        PyHamtNode_Bitmap *ret = hamt_node_bitmap_clone(self);
        if (ret == NULL) {
            Py_DECREF(sub_node);
            return NULL;
        }
        Py_SETREF(ret->b_array[key_idx], NULL);
        Py_SETREF(ret->b_array[val_idx], (PyObject *)sub_node);

        *added_leaf = 1;
        return (PyHamtNode *)ret;
    }
    else {
        /* There was no key before with the same (shift,hash). */

        uint32_t n = hamt_bitcount(self->b_bitmap);

        if (n >= 16) {
            /* When we have a situation where we want to store more
               than 16 nodes at one level of the tree, we no longer
               want to use the Bitmap node with bitmap encoding.

               Instead we start using an Array node, which has
               simpler (faster) implementation at the expense of
               having prealocated 32 pointers for its keys/values
               pairs.

               Small hamt objects (<30 keys) usually don't have any
               Array nodes at all.  Betwen ~30 and ~400 keys hamt
               objects usually have one Array node, and usually it's
               a root node.
            */

            uint32_t jdx = hamt_mask(hash, shift);
            /* 'jdx' is the index of where the new key should be added
               in the new Array node we're about to create. */

            PyHamtNode *empty = NULL;
            PyHamtNode_Array *new_node = NULL;
            PyHamtNode *res = NULL;

            /* Create a new Array node. */
            new_node = (PyHamtNode_Array *)hamt_node_array_new(n + 1);
            if (new_node == NULL) {
                goto fin;
            }

            /* Create an empty bitmap node for the next
               hamt_node_assoc call. */
            empty = hamt_node_bitmap_new(0);
            if (empty == NULL) {
                goto fin;
            }

            /* Make a new bitmap node for the key/val we're adding.
               Set that bitmap node to new-array-node[jdx]. */
            new_node->a_array[jdx] = hamt_node_assoc(
                empty, shift + 5, hash, key, val, added_leaf);
            if (new_node->a_array[jdx] == NULL) {
                goto fin;
            }

            /* Copy existing key/value pairs from the current Bitmap
               node to the new Array node we've just created. */
            Py_ssize_t i, j;
            for (i = 0, j = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
                if (((self->b_bitmap >> i) & 1) != 0) {
                    /* Ensure we don't accidentally override `jdx` element
                       we set few lines above.
                    */
                    assert(new_node->a_array[i] == NULL);

                    if (self->b_array[j] == NULL) {
                        new_node->a_array[i] =
                            (PyHamtNode *)self->b_array[j + 1];
                        Py_INCREF(new_node->a_array[i]);
                    }
                    else {
                        new_node->a_array[i] = hamt_node_assoc(
                            empty, shift + 5,
                            hamt_hash(self->b_array[j]),
                            self->b_array[j],
                            self->b_array[j + 1],
                            added_leaf);

                        if (new_node->a_array[i] == NULL) {
                            goto fin;
                        }
                    }
                    j += 2;
                }
            }

            VALIDATE_ARRAY_NODE(new_node)

            /* That's it! */
            res = (PyHamtNode *)new_node;

        fin:
            Py_XDECREF(empty);
            if (res == NULL) {
                Py_XDECREF(new_node);
            }
            return res;
        }
        else {
            /* We have less than 16 keys at this level; let's just
               create a new bitmap node out of this node with the
               new key/val pair added. */

            uint32_t key_idx = 2 * idx;
            uint32_t val_idx = key_idx + 1;
            Py_ssize_t i;

            *added_leaf = 1;

            /* Allocate new Bitmap node which can have one more key/val
               pair in addition to what we have already. */
            PyHamtNode_Bitmap *new_node =
                (PyHamtNode_Bitmap *)hamt_node_bitmap_new(2 * (n + 1));
            if (new_node == NULL) {
                return NULL;
            }

            /* Copy all keys/values that will be before the new key/value
               we are adding. */
            for (i = 0; i < key_idx; i++) {
                Py_XINCREF(self->b_array[i]);
                new_node->b_array[i] = self->b_array[i];
            }

            /* Set the new key/value to the new Bitmap node. */
            Py_INCREF(key);
            new_node->b_array[key_idx] = key;
            Py_INCREF(val);
            new_node->b_array[val_idx] = val;

            /* Copy all keys/values that will be after the new key/value
               we are adding. */
            for (i = key_idx; i < Py_SIZE(self); i++) {
                Py_XINCREF(self->b_array[i]);
                new_node->b_array[i + 2] = self->b_array[i];
            }

            new_node->b_bitmap = self->b_bitmap | bit;
            return (PyHamtNode *)new_node;
        }
    }
}

static hamt_without_t
hamt_node_bitmap_without(PyHamtNode_Bitmap *self,
                         uint32_t shift, int32_t hash,
                         PyObject *key,
                         PyHamtNode **new_node)
{
    uint32_t bit = hamt_bitpos(hash, shift);
    if ((self->b_bitmap & bit) == 0) {
        return W_NOT_FOUND;
    }

    uint32_t idx = hamt_bitindex(self->b_bitmap, bit);

    uint32_t key_idx = 2 * idx;
    uint32_t val_idx = key_idx + 1;

    PyObject *key_or_null = self->b_array[key_idx];
    PyObject *val_or_node = self->b_array[val_idx];

    if (key_or_null == NULL) {
        /* key == NULL means that 'value' is another tree node. */

        PyHamtNode *sub_node = NULL;

        hamt_without_t res = hamt_node_without(
            (PyHamtNode *)val_or_node,
            shift + 5, hash, key, &sub_node);

        switch (res) {
            case W_EMPTY:
                /* It's impossible for us to receive a W_EMPTY here:

                    - Array nodes are converted to Bitmap nodes when
                      we delete 16th item from them;

                    - Collision nodes are converted to Bitmap when
                      there is one item in them;

                    - Bitmap node's without() inlines single-item
                      sub-nodes.

                   So in no situation we can have a single-item
                   Bitmap child of another Bitmap node.
                 */
                abort();

            case W_NEWNODE: {
                assert(sub_node != NULL);

                if (IS_BITMAP_NODE(sub_node)) {
                    PyHamtNode_Bitmap *sub_tree = (PyHamtNode_Bitmap *)sub_node;
                    if (hamt_node_bitmap_count(sub_tree) == 1 &&
                            sub_tree->b_array[0] != NULL)
                    {
                        /* A bitmap node with one key/value pair.  Just
                           merge it into this node.

                           Note that we don't inline Bitmap nodes that
                           have a NULL key -- those nodes point to another
                           tree level, and we cannot simply move tree levels
                           up or down.
                        */

                        PyHamtNode_Bitmap *clone = hamt_node_bitmap_clone(self);
                        if (clone == NULL) {
                            Py_DECREF(sub_node);
                            return W_ERROR;
                        }

                        PyObject *key = sub_tree->b_array[0];
                        PyObject *val = sub_tree->b_array[1];

                        Py_INCREF(key);
                        Py_XSETREF(clone->b_array[key_idx], key);
                        Py_INCREF(val);
                        Py_SETREF(clone->b_array[val_idx], val);

                        Py_DECREF(sub_tree);

                        *new_node = (PyHamtNode *)clone;
                        return W_NEWNODE;
                    }
                }

#ifdef Py_DEBUG
                /* Ensure that Collision.without implementation
                   converts to Bitmap nodes itself.
                */
                if (IS_COLLISION_NODE(sub_node)) {
                    assert(hamt_node_collision_count(
                            (PyHamtNode_Collision*)sub_node) > 1);
                }
#endif

                PyHamtNode_Bitmap *clone = hamt_node_bitmap_clone(self);
                Py_SETREF(clone->b_array[val_idx],
                          (PyObject *)sub_node);  /* borrow */

                *new_node = (PyHamtNode *)clone;
                return W_NEWNODE;
            }

            case W_ERROR:
            case W_NOT_FOUND:
                assert(sub_node == NULL);
                return res;
        }
    }
    else {
        /* We have a regular key/value pair */

        int cmp = PyObject_RichCompareBool(key_or_null, key, Py_EQ);
        if (cmp < 0) {
            return W_ERROR;
        }
        if (cmp == 0) {
            return W_NOT_FOUND;
        }

        if (hamt_node_bitmap_count(self) == 1) {
            return W_EMPTY;
        }

        *new_node = (PyHamtNode *)
            hamt_node_bitmap_clone_without(self, bit);
        if (*new_node == NULL) {
            return W_ERROR;
        }

        return W_NEWNODE;
    }
}

static hamt_find_t
hamt_node_bitmap_find(PyHamtNode_Bitmap *self,
                      uint32_t shift, int32_t hash,
                      PyObject *key, PyObject **val)
{
    /* Lookup a key in a Bitmap node. */

    uint32_t bit = hamt_bitpos(hash, shift);
    uint32_t idx;
    uint32_t key_idx;
    uint32_t val_idx;
    PyObject *key_or_null;
    PyObject *val_or_node;
    int comp_err;

    if ((self->b_bitmap & bit) == 0) {
        return F_NOT_FOUND;
    }

    idx = hamt_bitindex(self->b_bitmap, bit);
    assert(idx >= 0);
    key_idx = idx * 2;
    val_idx = key_idx + 1;

    assert(val_idx < Py_SIZE(self));

    key_or_null = self->b_array[key_idx];
    val_or_node = self->b_array[val_idx];

    if (key_or_null == NULL) {
        /* There are a few keys that have the same hash at the current shift
           that match our key.  Dispatch the lookup further down the tree. */
        assert(val_or_node != NULL);
        return hamt_node_find((PyHamtNode *)val_or_node,
                              shift + 5, hash, key, val);
    }

    /* We have only one key -- a potential match.  Let's compare if the
       key we are looking at is equal to the key we are looking for. */
    assert(key != NULL);
    comp_err = PyObject_RichCompareBool(key, key_or_null, Py_EQ);
    if (comp_err < 0) {  /* exception in __eq__ */
        return F_ERROR;
    }
    if (comp_err == 1) {  /* key == key_or_null */
        *val = val_or_node;
        return F_FOUND;
    }

    return F_NOT_FOUND;
}

static int
hamt_node_bitmap_traverse(PyHamtNode_Bitmap *self, visitproc visit, void *arg)
{
    /* Bitmap's tp_traverse */

    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->b_array[i]);
    }

    return 0;
}

static void
hamt_node_bitmap_dealloc(PyHamtNode_Bitmap *self)
{
    /* Bitmap's tp_dealloc */

    Py_ssize_t len = Py_SIZE(self);
    Py_ssize_t i;

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    if (len > 0) {
        i = len;
        while (--i >= 0) {
            Py_XDECREF(self->b_array[i]);
        }

#if PyHamtNode_Bitmap_MAXSAVESIZE > 0
        /* Check if we can add this node to the freelist of Bitmap nodes. */
        if (len < PyHamtNode_Bitmap_MAXSAVESIZE &&
                numfree_bitmap[len] < PyHamtNode_Bitmap_MAXFREELIST)
        {
            self->b_array[0] = (PyObject*) free_bitmap_list[len];
            free_bitmap_list[len] = self;
            numfree_bitmap[len]++;
            goto done;
        }

#endif
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
done:
    Py_TRASHCAN_SAFE_END(self)
}

#ifdef Py_DEBUG
static int
hamt_node_bitmap_dump(PyHamtNode_Bitmap *node,
                      _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Bitmap nodes. */

    Py_ssize_t i;
    PyObject *tmp1;
    PyObject *tmp2;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "BitmapNode(size=%zd count=%zd ",
                          Py_SIZE(node), Py_SIZE(node) >> 1))
    {
        goto error;
    }

    tmp1 = PyLong_FromUnsignedLong(node->b_bitmap);
    if (tmp1 == NULL) {
        goto error;
    }
    tmp2 = _PyLong_Format(tmp1, 2);
    Py_DECREF(tmp1);
    if (tmp2 == NULL) {
        goto error;
    }
    if (_hamt_dump_format(writer, "bitmap=%S id=%p):\n", tmp2, node)) {
        Py_DECREF(tmp2);
        goto error;
    }
    Py_DECREF(tmp2);

    for (i = 0; i < Py_SIZE(node); i += 2) {
        PyObject *key_or_null = node->b_array[i];
        PyObject *val_or_node = node->b_array[i + 1];

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (key_or_null == NULL) {
            if (_hamt_dump_format(writer, "NULL:\n")) {
                goto error;
            }

            if (hamt_node_dump((PyHamtNode *)val_or_node,
                               writer, level + 2))
            {
                goto error;
            }
        }
        else {
            if (_hamt_dump_format(writer, "%R: %R", key_or_null,
                                  val_or_node))
            {
                goto error;
            }
        }

        if (_hamt_dump_format(writer, "\n")) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Collision Node


static PyHamtNode *
hamt_node_collision_new(int32_t hash, Py_ssize_t size)
{
    /* Create a new Collision node. */

    PyHamtNode_Collision *node;
    Py_ssize_t i;

    assert(size >= 4);
    assert(size % 2 == 0);

    node = PyObject_GC_NewVar(
        PyHamtNode_Collision, &_PyHamt_CollisionNode_Type, size);
    if (node == NULL) {
        return NULL;
    }

    for (i = 0; i < size; i++) {
        node->c_array[i] = NULL;
    }

    Py_SIZE(node) = size;
    node->c_hash = hash;

    _PyObject_GC_TRACK(node);

    return (PyHamtNode *)node;
}

static hamt_find_t
hamt_node_collision_find_index(PyHamtNode_Collision *self, PyObject *key,
                               Py_ssize_t *idx)
{
    /* Lookup `key` in the Collision node `self`.  Set the index of the
       found key to 'idx'. */

    Py_ssize_t i;
    PyObject *el;

    for (i = 0; i < Py_SIZE(self); i += 2) {
        el = self->c_array[i];

        assert(el != NULL);
        int cmp = PyObject_RichCompareBool(key, el, Py_EQ);
        if (cmp < 0) {
            return F_ERROR;
        }
        if (cmp == 1) {
            *idx = i;
            return F_FOUND;
        }
    }

    return F_NOT_FOUND;
}

static PyHamtNode *
hamt_node_collision_assoc(PyHamtNode_Collision *self,
                          uint32_t shift, int32_t hash,
                          PyObject *key, PyObject *val, int* added_leaf)
{
    /* Set a new key to this level (currently a Collision node)
       of the tree. */

    if (hash == self->c_hash) {
        /* The hash of the 'key' we are adding matches the hash of
           other keys in this Collision node. */

        Py_ssize_t key_idx = -1;
        hamt_find_t found;
        PyHamtNode_Collision *new_node;
        Py_ssize_t i;

        /* Let's try to lookup the new 'key', maybe we already have it. */
        found = hamt_node_collision_find_index(self, key, &key_idx);
        switch (found) {
            case F_ERROR:
                /* Exception. */
                return NULL;

            case F_NOT_FOUND:
                /* This is a totally new key.  Clone the current node,
                   add a new key/value to the cloned node. */

                new_node = (PyHamtNode_Collision *)hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self) + 2);
                if (new_node == NULL) {
                    return NULL;
                }

                for (i = 0; i < Py_SIZE(self); i++) {
                    Py_INCREF(self->c_array[i]);
                    new_node->c_array[i] = self->c_array[i];
                }

                Py_INCREF(key);
                new_node->c_array[i] = key;
                Py_INCREF(val);
                new_node->c_array[i + 1] = val;

                *added_leaf = 1;
                return (PyHamtNode *)new_node;

            case F_FOUND:
                /* There's a key which is equal to the key we are adding. */

                assert(key_idx >= 0);
                assert(key_idx < Py_SIZE(self));
                Py_ssize_t val_idx = key_idx + 1;

                /* Check if the existing value for the key is equal
                   to the value that we're setting. */
                int cmp = PyObject_RichCompareBool(
                    self->c_array[val_idx], val, Py_EQ);
                if (cmp < 0) {
                    /* Exception */
                    return NULL;
                }
                if (cmp == 1) {
                    /* We're setting a key/value pair that's already set. */
                    Py_INCREF(self);
                    return (PyHamtNode *)self;
                }

                /* We need to replace old value for the key
                   with a new value.  Create a new Collision node.*/
                new_node = (PyHamtNode_Collision *)hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self));
                if (new_node == NULL) {
                    return NULL;
                }

                /* Copy all elements of the old node to the new one. */
                for (i = 0; i < Py_SIZE(self); i++) {
                    Py_INCREF(self->c_array[i]);
                    new_node->c_array[i] = self->c_array[i];
                }

                /* Replace the old value with the new value for the our key. */
                Py_DECREF(new_node->c_array[val_idx]);
                Py_INCREF(val);
                new_node->c_array[val_idx] = val;

                return (PyHamtNode *)new_node;
        }
    }
    else {
        /* The hash of the new key is different from the hash that
           all keys of this Collision node have.

           Create a Bitmap node inplace with two children:
           key/value pair that we're adding, and the Collision node
           we're replacing on this tree level.
        */

        PyHamtNode_Bitmap *new_node;
        PyHamtNode *assoc_res;

        new_node = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(2);
        if (new_node == NULL) {
            return NULL;
        }
        new_node->b_bitmap = hamt_bitpos(self->c_hash, shift);
        Py_INCREF(self);
        new_node->b_array[1] = (PyObject*) self;

        assoc_res = hamt_node_bitmap_assoc(
            new_node, shift, hash, key, val, added_leaf);
        Py_DECREF(new_node);
        return assoc_res;
    }
}

static inline Py_ssize_t
hamt_node_collision_count(PyHamtNode_Collision *node)
{
    return Py_SIZE(node) >> 1;
}

static hamt_without_t
hamt_node_collision_without(PyHamtNode_Collision *self,
                            uint32_t shift, int32_t hash,
                            PyObject *key,
                            PyHamtNode **new_node)
{
    if (hash != self->c_hash) {
        return W_NOT_FOUND;
    }

    Py_ssize_t key_idx = -1;
    hamt_find_t found = hamt_node_collision_find_index(self, key, &key_idx);

    switch (found) {
        case F_ERROR:
            return W_ERROR;

        case F_NOT_FOUND:
            return W_NOT_FOUND;

        case F_FOUND:
            assert(key_idx >= 0);
            assert(key_idx < Py_SIZE(self));

            Py_ssize_t new_count = hamt_node_collision_count(self) - 1;

            if (new_count == 0) {
                /* The node has only one key/value pair and it's for the
                   key we're trying to delete.  So a new node will be empty
                   after the removal.
                */
                return W_EMPTY;
            }

            if (new_count == 1) {
                /* The node has two keys, and after deletion the
                   new Collision node would have one.  Collision nodes
                   with one key shouldn't exist, co convert it to a
                   Bitmap node.
                */
                PyHamtNode_Bitmap *node = (PyHamtNode_Bitmap *)
                    hamt_node_bitmap_new(2);
                if (node == NULL) {
                    return W_ERROR;
                }

                if (key_idx == 0) {
                    Py_INCREF(self->c_array[2]);
                    node->b_array[0] = self->c_array[2];
                    Py_INCREF(self->c_array[3]);
                    node->b_array[1] = self->c_array[3];
                }
                else {
                    assert(key_idx == 2);
                    Py_INCREF(self->c_array[0]);
                    node->b_array[0] = self->c_array[0];
                    Py_INCREF(self->c_array[1]);
                    node->b_array[1] = self->c_array[1];
                }

                node->b_bitmap = hamt_bitpos(hash, shift);

                *new_node = (PyHamtNode *)node;
                return W_NEWNODE;
            }

            /* Allocate a new Collision node with capacity for one
               less key/value pair */
            PyHamtNode_Collision *new = (PyHamtNode_Collision *)
                hamt_node_collision_new(
                    self->c_hash, Py_SIZE(self) - 2);

            /* Copy all other keys from `self` to `new` */
            Py_ssize_t i;
            for (i = 0; i < key_idx; i++) {
                Py_INCREF(self->c_array[i]);
                new->c_array[i] = self->c_array[i];
            }
            for (i = key_idx + 2; i < Py_SIZE(self); i++) {
                Py_INCREF(self->c_array[i]);
                new->c_array[i - 2] = self->c_array[i];
            }

            *new_node = (PyHamtNode*)new;
            return W_NEWNODE;
    }
}

static hamt_find_t
hamt_node_collision_find(PyHamtNode_Collision *self,
                         uint32_t shift, int32_t hash,
                         PyObject *key, PyObject **val)
{
    /* Lookup `key` in the Collision node `self`.  Set the value
       for the found key to 'val'. */

    Py_ssize_t idx = -1;
    hamt_find_t res;

    res = hamt_node_collision_find_index(self, key, &idx);
    if (res == F_ERROR || res == F_NOT_FOUND) {
        return res;
    }

    assert(idx >= 0);
    assert(idx + 1 < Py_SIZE(self));

    *val = self->c_array[idx + 1];
    assert(*val != NULL);

    return F_FOUND;
}


static int
hamt_node_collision_traverse(PyHamtNode_Collision *self,
                             visitproc visit, void *arg)
{
    /* Collision's tp_traverse */

    Py_ssize_t i;

    for (i = Py_SIZE(self); --i >= 0; ) {
        Py_VISIT(self->c_array[i]);
    }

    return 0;
}

static void
hamt_node_collision_dealloc(PyHamtNode_Collision *self)
{
    /* Collision's tp_dealloc */

    Py_ssize_t len = Py_SIZE(self);

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    if (len > 0) {

        while (--len >= 0) {
            Py_XDECREF(self->c_array[len]);
        }
    }

    Py_TYPE(self)->tp_free((PyObject *)self);
    Py_TRASHCAN_SAFE_END(self)
}

#ifdef Py_DEBUG
static int
hamt_node_collision_dump(PyHamtNode_Collision *node,
                         _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Collision nodes. */

    Py_ssize_t i;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "CollisionNode(size=%zd id=%p):\n",
                          Py_SIZE(node), node))
    {
        goto error;
    }

    for (i = 0; i < Py_SIZE(node); i += 2) {
        PyObject *key = node->c_array[i];
        PyObject *val = node->c_array[i + 1];

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "%R: %R\n", key, val)) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Array Node


static PyHamtNode *
hamt_node_array_new(Py_ssize_t count)
{
    /* Create a new Array node. */

    PyHamtNode_Array *node;
    Py_ssize_t i;

#if PyHamtNode_Array_MAXFREELIST > 0
    /* Check if there's a node in the freelist. */
    if ((node = free_array_list) != NULL) {
        free_array_list = (PyHamtNode_Array *)node->a_array[0];
        numfree_array--;
        _Py_NewReference((PyObject *)node);
    }
    else
#endif
    {
        /* Allocate a new Array node. */
        node = PyObject_GC_New(PyHamtNode_Array, &_PyHamt_ArrayNode_Type);
        if (node == NULL) {
            return NULL;
        }
    }

    for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
        node->a_array[i] = NULL;
    }

    node->a_count = count;

    _PyObject_GC_TRACK(node);
    return (PyHamtNode *)node;
}

static PyHamtNode_Array *
hamt_node_array_clone(PyHamtNode_Array *node)
{
    PyHamtNode_Array *clone;
    Py_ssize_t i;

    VALIDATE_ARRAY_NODE(node)

    /* Create a new Array node. */
    clone = (PyHamtNode_Array *)hamt_node_array_new(node->a_count);
    if (clone == NULL) {
        return NULL;
    }

    /* Copy all elements from the current Array node to the new one. */
    for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
        Py_XINCREF(node->a_array[i]);
        clone->a_array[i] = node->a_array[i];
    }

    VALIDATE_ARRAY_NODE(clone)
    return clone;
}

static PyHamtNode *
hamt_node_array_assoc(PyHamtNode_Array *self,
                      uint32_t shift, int32_t hash,
                      PyObject *key, PyObject *val, int* added_leaf)
{
    /* Set a new key to this level (currently a Collision node)
       of the tree.

       Array nodes don't store values, they can only point to
       other nodes.  They are simple arrays of 32 BaseNode pointers/
     */

    uint32_t idx = hamt_mask(hash, shift);
    PyHamtNode *node = self->a_array[idx];
    PyHamtNode *child_node;
    PyHamtNode_Array *new_node;
    Py_ssize_t i;

    if (node == NULL) {
        /* There's no child node for the given hash.  Create a new
           Bitmap node for this key. */

        PyHamtNode_Bitmap *empty = NULL;

        /* Get an empty Bitmap node to work with. */
        empty = (PyHamtNode_Bitmap *)hamt_node_bitmap_new(0);
        if (empty == NULL) {
            return NULL;
        }

        /* Set key/val to the newly created empty Bitmap, thus
           creating a new Bitmap node with our key/value pair. */
        child_node = hamt_node_bitmap_assoc(
            empty,
            shift + 5, hash, key, val, added_leaf);
        Py_DECREF(empty);
        if (child_node == NULL) {
            return NULL;
        }

        /* Create a new Array node. */
        new_node = (PyHamtNode_Array *)hamt_node_array_new(self->a_count + 1);
        if (new_node == NULL) {
            Py_DECREF(child_node);
            return NULL;
        }

        /* Copy all elements from the current Array node to the
           new one. */
        for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
            Py_XINCREF(self->a_array[i]);
            new_node->a_array[i] = self->a_array[i];
        }

        assert(new_node->a_array[idx] == NULL);
        new_node->a_array[idx] = child_node;  /* borrow */
        VALIDATE_ARRAY_NODE(new_node)
    }
    else {
        /* There's a child node for the given hash.
           Set the key to it./ */
        child_node = hamt_node_assoc(
            node, shift + 5, hash, key, val, added_leaf);
        if (child_node == (PyHamtNode *)self) {
            Py_DECREF(child_node);
            return (PyHamtNode *)self;
        }

        new_node = hamt_node_array_clone(self);
        if (new_node == NULL) {
            Py_DECREF(child_node);
            return NULL;
        }

        Py_SETREF(new_node->a_array[idx], child_node);  /* borrow */
        VALIDATE_ARRAY_NODE(new_node)
    }

    return (PyHamtNode *)new_node;
}

static hamt_without_t
hamt_node_array_without(PyHamtNode_Array *self,
                        uint32_t shift, int32_t hash,
                        PyObject *key,
                        PyHamtNode **new_node)
{
    uint32_t idx = hamt_mask(hash, shift);
    PyHamtNode *node = self->a_array[idx];

    if (node == NULL) {
        return W_NOT_FOUND;
    }

    PyHamtNode *sub_node = NULL;
    hamt_without_t res = hamt_node_without(
        (PyHamtNode *)node,
        shift + 5, hash, key, &sub_node);

    switch (res) {
        case W_NOT_FOUND:
        case W_ERROR:
            assert(sub_node == NULL);
            return res;

        case W_NEWNODE: {
            /* We need to replace a node at the `idx` index.
               Clone this node and replace.
            */
            assert(sub_node != NULL);

            PyHamtNode_Array *clone = hamt_node_array_clone(self);
            if (clone == NULL) {
                Py_DECREF(sub_node);
                return W_ERROR;
            }

            Py_SETREF(clone->a_array[idx], sub_node);  /* borrow */
            *new_node = (PyHamtNode*)clone;  /* borrow */
            return W_NEWNODE;
        }

        case W_EMPTY: {
            assert(sub_node == NULL);
            /* We need to remove a node at the `idx` index.
               Calculate the size of the replacement Array node.
            */
            Py_ssize_t new_count = self->a_count - 1;

            if (new_count == 0) {
                return W_EMPTY;
            }

            if (new_count >= 16) {
                /* We convert Bitmap nodes to Array nodes, when a
                   Bitmap node needs to store more than 15 key/value
                   pairs.  So we will create a new Array node if we
                   the number of key/values after deletion is still
                   greater than 15.
                */

                PyHamtNode_Array *new = hamt_node_array_clone(self);
                if (new == NULL) {
                    return W_ERROR;
                }
                new->a_count = new_count;
                Py_CLEAR(new->a_array[idx]);

                *new_node = (PyHamtNode*)new;  /* borrow */
                return W_NEWNODE;
            }

            /* New Array node would have less than 16 key/value
               pairs.  We need to create a replacement Bitmap node. */

            Py_ssize_t bitmap_size = new_count * 2;
            uint32_t bitmap = 0;

            PyHamtNode_Bitmap *new = (PyHamtNode_Bitmap *)
                hamt_node_bitmap_new(bitmap_size);
            if (new == NULL) {
                return W_ERROR;
            }

            Py_ssize_t new_i = 0;
            for (uint32_t i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
                if (i == idx) {
                    /* Skip the node we are deleting. */
                    continue;
                }

                PyHamtNode *node = self->a_array[i];
                if (node == NULL) {
                    /* Skip any missing nodes. */
                    continue;
                }

                bitmap |= 1 << i;

                if (IS_BITMAP_NODE(node)) {
                    PyHamtNode_Bitmap *child = (PyHamtNode_Bitmap *)node;

                    if (hamt_node_bitmap_count(child) == 1 &&
                            child->b_array[0] != NULL)
                    {
                        /* node is a Bitmap with one key/value pair, just
                           merge it into the new Bitmap node we're building.

                           Note that we don't inline Bitmap nodes that
                           have a NULL key -- those nodes point to another
                           tree level, and we cannot simply move tree levels
                           up or down.
                        */
                        PyObject *key = child->b_array[0];
                        PyObject *val = child->b_array[1];

                        Py_INCREF(key);
                        new->b_array[new_i] = key;
                        Py_INCREF(val);
                        new->b_array[new_i + 1] = val;
                    }
                    else {
                        new->b_array[new_i] = NULL;
                        Py_INCREF(node);
                        new->b_array[new_i + 1] = (PyObject*)node;
                    }
                }
                else {

#ifdef Py_DEBUG
                    if (IS_COLLISION_NODE(node)) {
                        Py_ssize_t child_count = hamt_node_collision_count(
                            (PyHamtNode_Collision*)node);
                        assert(child_count > 1);
                    }
                    else if (IS_ARRAY_NODE(node)) {
                        assert(((PyHamtNode_Array*)node)->a_count >= 16);
                    }
#endif

                    /* Just copy the node into our new Bitmap */
                    new->b_array[new_i] = NULL;
                    Py_INCREF(node);
                    new->b_array[new_i + 1] = (PyObject*)node;
                }

                new_i += 2;
            }

            new->b_bitmap = bitmap;
            *new_node = (PyHamtNode*)new;  /* borrow */
            return W_NEWNODE;
        }
    }
}

static hamt_find_t
hamt_node_array_find(PyHamtNode_Array *self,
                     uint32_t shift, int32_t hash,
                     PyObject *key, PyObject **val)
{
    /* Lookup `key` in the Array node `self`.  Set the value
       for the found key to 'val'. */

    uint32_t idx = hamt_mask(hash, shift);
    PyHamtNode *node;

    node = self->a_array[idx];
    if (node == NULL) {
        return F_NOT_FOUND;
    }

    /* Dispatch to the generic hamt_node_find */
    return hamt_node_find(node, shift + 5, hash, key, val);
}

static int
hamt_node_array_traverse(PyHamtNode_Array *self,
                         visitproc visit, void *arg)
{
    /* Array's tp_traverse */

    Py_ssize_t i;

    for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
        Py_VISIT(self->a_array[i]);
    }

    return 0;
}

static void
hamt_node_array_dealloc(PyHamtNode_Array *self)
{
    /* Array's tp_dealloc */

    Py_ssize_t i;

    PyObject_GC_UnTrack(self);
    Py_TRASHCAN_SAFE_BEGIN(self)

    for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
        Py_XDECREF(self->a_array[i]);
    }

#if PyHamtNode_Array_MAXFREELIST > 0
    if (numfree_array < PyHamtNode_Array_MAXFREELIST) {
        self->a_array[0] = (PyHamtNode *)free_array_list;
        free_array_list = self;
        numfree_array++;
        goto done;
    }
#endif

    Py_TYPE(self)->tp_free((PyObject *)self);
done:
    Py_TRASHCAN_SAFE_END(self)
}

#ifdef Py_DEBUG
static int
hamt_node_array_dump(PyHamtNode_Array *node,
                     _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for Array nodes. */

    Py_ssize_t i;

    if (_hamt_dump_ident(writer, level + 1)) {
        goto error;
    }

    if (_hamt_dump_format(writer, "ArrayNode(id=%p):\n", node)) {
        goto error;
    }

    for (i = 0; i < HAMT_ARRAY_NODE_SIZE; i++) {
        if (node->a_array[i] == NULL) {
            continue;
        }

        if (_hamt_dump_ident(writer, level + 2)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "%d::\n", i)) {
            goto error;
        }

        if (hamt_node_dump(node->a_array[i], writer, level + 1)) {
            goto error;
        }

        if (_hamt_dump_format(writer, "\n")) {
            goto error;
        }
    }

    return 0;
error:
    return -1;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Node Dispatch


static PyHamtNode *
hamt_node_assoc(PyHamtNode *node,
                uint32_t shift, int32_t hash,
                PyObject *key, PyObject *val, int* added_leaf)
{
    /* Set key/value to the 'node' starting with the given shift/hash.
       Return a new node, or the same node if key/value already
       set.

       added_leaf will be set to 1 if key/value wasn't in the
       tree before.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype}_assoc method.
    */

    if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_assoc(
            (PyHamtNode_Bitmap *)node,
            shift, hash, key, val, added_leaf);
    }
    else if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_assoc(
            (PyHamtNode_Array *)node,
            shift, hash, key, val, added_leaf);
    }
    else {
        assert(IS_COLLISION_NODE(node));
        return hamt_node_collision_assoc(
            (PyHamtNode_Collision *)node,
            shift, hash, key, val, added_leaf);
    }
}

static hamt_without_t
hamt_node_without(PyHamtNode *node,
                  uint32_t shift, int32_t hash,
                  PyObject *key,
                  PyHamtNode **new_node)
{
    if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_without(
            (PyHamtNode_Bitmap *)node,
            shift, hash, key,
            new_node);
    }
    else if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_without(
            (PyHamtNode_Array *)node,
            shift, hash, key,
            new_node);
    }
    else {
        assert(IS_COLLISION_NODE(node));
        return hamt_node_collision_without(
            (PyHamtNode_Collision *)node,
            shift, hash, key,
            new_node);
    }
}

static hamt_find_t
hamt_node_find(PyHamtNode *node,
               uint32_t shift, int32_t hash,
               PyObject *key, PyObject **val)
{
    /* Find the key in the node starting with the given shift/hash.

       If a value is found, the result will be set to F_FOUND, and
       *val will point to the found value object.

       If a value wasn't found, the result will be set to F_NOT_FOUND.

       If an exception occurs during the call, the result will be F_ERROR.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype}_find method.
    */

    if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_find(
            (PyHamtNode_Bitmap *)node,
            shift, hash, key, val);

    }
    else if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_find(
            (PyHamtNode_Array *)node,
            shift, hash, key, val);
    }
    else {
        assert(IS_COLLISION_NODE(node));
        return hamt_node_collision_find(
            (PyHamtNode_Collision *)node,
            shift, hash, key, val);
    }
}

#ifdef Py_DEBUG
static int
hamt_node_dump(PyHamtNode *node,
               _PyUnicodeWriter *writer, int level)
{
    /* Debug build: __dump__() method implementation for a node.

       This method automatically dispatches to the suitable
       hamt_node_{nodetype})_dump method.
    */

    if (IS_BITMAP_NODE(node)) {
        return hamt_node_bitmap_dump(
            (PyHamtNode_Bitmap *)node, writer, level);
    }
    else if (IS_ARRAY_NODE(node)) {
        return hamt_node_array_dump(
            (PyHamtNode_Array *)node, writer, level);
    }
    else {
        assert(IS_COLLISION_NODE(node));
        return hamt_node_collision_dump(
            (PyHamtNode_Collision *)node, writer, level);
    }
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Iterators: Machinery


static hamt_iter_t
hamt_iterator_next(PyHamtIteratorState *iter, PyObject **key, PyObject **val);


static void
hamt_iterator_init(PyHamtIteratorState *iter, PyHamtNode *root)
{
    for (uint32_t i = 0; i < HAMT_MAX_TREE_DEPTH; i++) {
        iter->i_nodes[i] = NULL;
        iter->i_pos[i] = 0;
    }

    iter->i_level = 0;

    /* Note: we don't incref/decref nodes in i_nodes. */
    iter->i_nodes[0] = root;
}

static hamt_iter_t
hamt_iterator_bitmap_next(PyHamtIteratorState *iter,
                          PyObject **key, PyObject **val)
{
    int8_t level = iter->i_level;

    PyHamtNode_Bitmap *node = (PyHamtNode_Bitmap *)(iter->i_nodes[level]);
    Py_ssize_t pos = iter->i_pos[level];

    if (pos + 1 >= Py_SIZE(node)) {
#ifdef Py_DEBUG
        assert(iter->i_level >= 0);
        iter->i_nodes[iter->i_level] = NULL;
#endif
        iter->i_level--;
        return hamt_iterator_next(iter, key, val);
    }

    if (node->b_array[pos] == NULL) {
        iter->i_pos[level] = pos + 2;

        int8_t next_level = level + 1;
        assert(next_level < HAMT_MAX_TREE_DEPTH);
        iter->i_level = next_level;
        iter->i_pos[next_level] = 0;
        iter->i_nodes[next_level] = (PyHamtNode *)
            node->b_array[pos + 1];

        return hamt_iterator_next(iter, key, val);
    }

    *key = node->b_array[pos];
    *val = node->b_array[pos + 1];
    iter->i_pos[level] = pos + 2;
    return I_ITEM;
}

static hamt_iter_t
hamt_iterator_collision_next(PyHamtIteratorState *iter,
                             PyObject **key, PyObject **val)
{
    int8_t level = iter->i_level;

    PyHamtNode_Collision *node = (PyHamtNode_Collision *)(iter->i_nodes[level]);
    Py_ssize_t pos = iter->i_pos[level];

    if (pos + 1 >= Py_SIZE(node)) {
#ifdef Py_DEBUG
        assert(iter->i_level >= 0);
        iter->i_nodes[iter->i_level] = NULL;
#endif
        iter->i_level--;
        return hamt_iterator_next(iter, key, val);
    }

    *key = node->c_array[pos];
    *val = node->c_array[pos + 1];
    iter->i_pos[level] = pos + 2;
    return I_ITEM;
}

static hamt_iter_t
hamt_iterator_array_next(PyHamtIteratorState *iter,
                         PyObject **key, PyObject **val)
{
    int8_t level = iter->i_level;

    PyHamtNode_Array *node = (PyHamtNode_Array *)(iter->i_nodes[level]);
    Py_ssize_t pos = iter->i_pos[level];

    if (pos >= HAMT_ARRAY_NODE_SIZE) {
#ifdef Py_DEBUG
        assert(iter->i_level >= 0);
        iter->i_nodes[iter->i_level] = NULL;
#endif
        iter->i_level--;
        return hamt_iterator_next(iter, key, val);
    }

    for (Py_ssize_t i = pos; i < HAMT_ARRAY_NODE_SIZE; i++) {
        if (node->a_array[i] != NULL) {
            iter->i_pos[level] = i + 1;

            int8_t next_level = level + 1;
            assert(next_level < HAMT_MAX_TREE_DEPTH);
            iter->i_pos[next_level] = 0;
            iter->i_nodes[next_level] = node->a_array[i];
            iter->i_level = next_level;

            return hamt_iterator_next(iter, key, val);
        }
    }

#ifdef Py_DEBUG
        assert(iter->i_level >= 0);
        iter->i_nodes[iter->i_level] = NULL;
#endif

    iter->i_level--;
    return hamt_iterator_next(iter, key, val);
}

static hamt_iter_t
hamt_iterator_next(PyHamtIteratorState *iter, PyObject **key, PyObject **val)
{
    if (iter->i_level < 0) {
        return I_END;
    }

    assert(iter->i_level < HAMT_MAX_TREE_DEPTH);

    PyHamtNode *current = iter->i_nodes[iter->i_level];

    if (IS_BITMAP_NODE(current)) {
        return hamt_iterator_bitmap_next(iter, key, val);
    }
    else if (IS_ARRAY_NODE(current)) {
        return hamt_iterator_array_next(iter, key, val);
    }
    else {
        assert(IS_COLLISION_NODE(current));
        return hamt_iterator_collision_next(iter, key, val);
    }
}


/////////////////////////////////// HAMT high-level functions


static PyHamtObject *
hamt_assoc(PyHamtObject *o, PyObject *key, PyObject *val)
{
    int32_t key_hash;
    int added_leaf = 0;
    PyHamtNode *new_root;
    PyHamtObject *new_o;

    key_hash = hamt_hash(key);
    if (key_hash == -1) {
        return NULL;
    }

    new_root = hamt_node_assoc(
        (PyHamtNode *)(o->h_root),
        0, key_hash, key, val, &added_leaf);
    if (new_root == NULL) {
        return NULL;
    }

    if (new_root == o->h_root) {
        Py_DECREF(new_root);
        Py_INCREF(o);
        return o;
    }

    new_o = hamt_alloc();
    if (new_o == NULL) {
        Py_DECREF(new_root);
        return NULL;
    }

    new_o->h_root = new_root;  /* borrow */
    new_o->h_count = added_leaf ? o->h_count + 1 : o->h_count;

    return new_o;
}

static PyHamtObject *
hamt_without(PyHamtObject *o, PyObject *key)
{
    int32_t key_hash = hamt_hash(key);
    if (key_hash == -1) {
        return NULL;
    }

    PyHamtNode *new_root;

    hamt_without_t res = hamt_node_without(
        (PyHamtNode *)(o->h_root),
        0, key_hash, key,
        &new_root);

    switch (res) {
        case W_ERROR:
            return NULL;
        case W_EMPTY:
            return hamt_new();
        case W_NOT_FOUND:
            Py_INCREF(o);
            return o;
        case W_NEWNODE: {
            PyHamtObject *new_o = hamt_alloc();
            if (new_o == NULL) {
                Py_DECREF(new_root);
                return NULL;
            }

            new_o->h_root = new_root;  /* borrow */
            new_o->h_count = o->h_count - 1;
            assert(new_o->h_count >= 0);
            return new_o;
        }
    }
}

static hamt_find_t
hamt_find(PyHamtObject *o, PyObject *key, PyObject **val)
{
    int32_t key_hash;

    key_hash = hamt_hash(key);
    if (key_hash == -1) {
        return F_ERROR;
    }

    return hamt_node_find(o->h_root, 0, key_hash, key, val);
}

static int
hamt_eq(PyHamtObject *v, PyHamtObject *w)
{
    if (v->h_count != w->h_count) {
        return 0;
    }

    PyHamtIteratorState iter;
    hamt_iter_t iter_res;
    hamt_find_t find_res;
    PyObject *v_key;
    PyObject *v_val;
    PyObject *w_val;

    hamt_iterator_init(&iter, v->h_root);

    do {
        iter_res = hamt_iterator_next(&iter, &v_key, &v_val);
        if (iter_res == I_ITEM) {
            find_res = hamt_find(w, v_key, &w_val);
            switch (find_res) {
                case F_ERROR:
                    return -1;

                case F_NOT_FOUND:
                    return 0;

                case F_FOUND: {
                    int cmp = PyObject_RichCompareBool(v_val, w_val, Py_EQ);
                    if (cmp < 0) {
                        return -1;
                    }
                    if (cmp == 0) {
                        return 0;
                    }
                }
            }
        }
    } while (iter_res != I_END);

    return 1;
}

static Py_ssize_t
hamt_len(PyHamtObject *o)
{
    return o->h_count;
}

static int
hamt_contains(PyHamtObject *self, PyObject *key)
{
    PyObject *val;
    hamt_find_t res = hamt_find(self, key, &val);
    switch (res) {
        case F_ERROR:
            return -1;
        case F_FOUND:
            return 1;
        case F_NOT_FOUND:
            return 0;
    }
}

static PyObject *
hamt_subscript(PyHamtObject *self, PyObject *key)
{
    PyObject *val;
    hamt_find_t res = hamt_find(self, key, &val);
    switch (res) {
        case F_ERROR:
            return NULL;
        case F_FOUND:
            Py_INCREF(val);
            return val;
        case F_NOT_FOUND:
            PyErr_SetObject(PyExc_KeyError, key);
            return NULL;
    }
}

static PyObject *
hamt_get(PyHamtObject *self, PyObject *key, PyObject *def)
{
    PyObject *val = NULL;
    hamt_find_t res = hamt_find(self, key, &val);
    switch (res) {
        case F_ERROR:
            return NULL;
        case F_FOUND:
            Py_INCREF(val);
            return val;
        case F_NOT_FOUND:
            if (def == NULL) {
                Py_RETURN_NONE;
            }
            Py_INCREF(def);
            return def;
    }
}

static PyHamtObject *
hamt_alloc(void)
{
    PyHamtObject *o;
    o = PyObject_GC_New(PyHamtObject, &_PyHamt_Type);
    if (o == NULL) {
        return NULL;
    }
    o->h_weakreflist = NULL;
    PyObject_GC_Track(o);
    return o;
}

static PyHamtObject *
hamt_new(void)
{
    PyHamtObject *o = hamt_alloc();
    if (o == NULL) {
        return NULL;
    }

    o->h_root = hamt_node_bitmap_new(0);
    if (o->h_root == NULL) {
        Py_DECREF(o);
        return NULL;
    }

    o->h_count = 0;
    return o;
}

#ifdef Py_DEBUG
static PyObject *
hamt_dump(PyHamtObject *self)
{
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);

    if (_hamt_dump_format(&writer, "HAMT(len=%zd):\n", self->h_count)) {
        goto error;
    }

    if (hamt_node_dump(self->h_root, &writer, 0)) {
        goto error;
    }

    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}
#endif  /* Py_DEBUG */


/////////////////////////////////// Iterators: Shared Iterator Implementation


static int
hamt_baseiter_tp_clear(PyHamtIterator *it)
{
    Py_CLEAR(it->hi_obj);
    return 0;
}

static void
hamt_baseiter_tp_dealloc(PyHamtIterator *it)
{
    PyObject_GC_UnTrack(it);
    (void)hamt_baseiter_tp_clear(it);
    PyObject_GC_Del(it);
}

static int
hamt_baseiter_tp_traverse(PyHamtIterator *it, visitproc visit, void *arg)
{
    Py_VISIT(it->hi_obj);
    return 0;
}

static PyObject *
hamt_baseiter_tp_iternext(PyHamtIterator *it)
{
    PyObject *key;
    PyObject *val;
    hamt_iter_t res = hamt_iterator_next(&it->hi_iter, &key, &val);

    switch (res) {
        case I_END:
            PyErr_SetNone(PyExc_StopIteration);
            return NULL;

        case I_ITEM: {
            return (*(it->hi_yield))(key, val);
        }
    }
}

static Py_ssize_t
hamt_baseiter_tp_len(PyHamtIterator *it)
{
    return it->hi_obj->h_count;
}

static PyMappingMethods PyHamtIterator_as_mapping = {
    (lenfunc)hamt_baseiter_tp_len,
};

static PyObject *
hamt_baseiter_new(PyTypeObject *type, binaryfunc yield, PyHamtObject *o)
{
    PyHamtIterator *it = PyObject_GC_New(PyHamtIterator, type);
    if (it == NULL) {
        return NULL;
    }

    Py_INCREF(o);
    it->hi_obj = o;
    it->hi_yield = yield;

    hamt_iterator_init(&it->hi_iter, o->h_root);

    return (PyObject*)it;
}

#define ITERATOR_TYPE_SHARED_SLOTS                              \
    .tp_basicsize = sizeof(PyHamtIterator),                     \
    .tp_itemsize = 0,                                           \
    .tp_as_mapping = &PyHamtIterator_as_mapping,                \
    .tp_dealloc = (destructor)hamt_baseiter_tp_dealloc,         \
    .tp_getattro = PyObject_GenericGetAttr,                     \
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,        \
    .tp_traverse = (traverseproc)hamt_baseiter_tp_traverse,     \
    .tp_clear = (inquiry)hamt_baseiter_tp_clear,                \
    .tp_iter = PyObject_SelfIter,                               \
    .tp_iternext = (iternextfunc)hamt_baseiter_tp_iternext,


/////////////////////////////////// _PyHamtItems_Type


PyTypeObject _PyHamtItems_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "items",
    ITERATOR_TYPE_SHARED_SLOTS
};

static PyObject *
hamt_iter_yield_items(PyObject *key, PyObject *val)
{
    PyObject *tup = PyTuple_New(2);
    if (tup == NULL) {
        return tup;
    }

    Py_INCREF(key);
    PyTuple_SET_ITEM(tup, 0, key);
    Py_INCREF(val);
    PyTuple_SET_ITEM(tup, 1, val);

    return tup;
}

static PyObject *
hamt_iter_new_items(PyHamtObject *o)
{
    return hamt_baseiter_new(
        &_PyHamtItems_Type, hamt_iter_yield_items, o);
}


/////////////////////////////////// _PyHamtKeys_Type


PyTypeObject _PyHamtKeys_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "keys",
    ITERATOR_TYPE_SHARED_SLOTS
};

static PyObject *
hamt_iter_yield_keys(PyObject *key, PyObject *val)
{
    Py_INCREF(key);
    return key;
}

static PyObject *
hamt_iter_new_keys(PyHamtObject *o)
{
    return hamt_baseiter_new(
        &_PyHamtKeys_Type, hamt_iter_yield_keys, o);
}


/////////////////////////////////// _PyHamtValues_Type


PyTypeObject _PyHamtValues_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "values",
    ITERATOR_TYPE_SHARED_SLOTS
};

static PyObject *
hamt_iter_yield_values(PyObject *key, PyObject *val)
{
    Py_INCREF(val);
    return val;
}

static PyObject *
hamt_iter_new_values(PyHamtObject *o)
{
    return hamt_baseiter_new(
        &_PyHamtValues_Type, hamt_iter_yield_values, o);
}


/////////////////////////////////// _PyHamt_Type


#ifdef Py_DEBUG
static PyObject *
hamt_dump(PyHamtObject *self);
#endif


static PyObject *
hamt_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    return (PyObject*)hamt_new();
}

static int
hamt_tp_clear(PyHamtObject *self)
{
    Py_CLEAR(self->h_root);
    return 0;
}


static int
hamt_tp_traverse(PyHamtObject *self, visitproc visit, void *arg)
{
    Py_VISIT(self->h_root);
    return 0;
}

static void
hamt_tp_dealloc(PyHamtObject *self)
{
    PyObject_GC_UnTrack(self);
    if (self->h_weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*)self);
    }
    (void)hamt_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}


static PyObject *
hamt_tp_richcompare(PyObject *v, PyObject *w, int op)
{
    if (!PyHamt_Check(v) || !PyHamt_Check(w) || (op != Py_EQ && op != Py_NE)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int res = hamt_eq((PyHamtObject *)v, (PyHamtObject *)w);
    if (res < 0) {
        return NULL;
    }

    if (op == Py_NE) {
        res = !res;
    }

    if (res) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

static int
hamt_tp_contains(PyHamtObject *self, PyObject *key)
{
    return hamt_contains(self, key);
}

static PyObject *
hamt_tp_subscript(PyHamtObject *self, PyObject *key)
{
    return hamt_subscript(self, key);
}

static Py_ssize_t
hamt_tp_len(PyHamtObject *self)
{
    return hamt_len(self);
}

static PyObject *
hamt_tp_iter(PyHamtObject *self)
{
    return hamt_iter_new_keys(self);
}

static PyObject *
hamt_py_set(PyHamtObject *self, PyObject *args)
{
    PyObject *key;
    PyObject *val;

    if (!PyArg_UnpackTuple(args, "set", 2, 2, &key, &val)) {
        return NULL;
    }

    return (PyObject *)hamt_assoc(self, key, val);
}

static PyObject *
hamt_py_get(PyHamtObject *self, PyObject *args)
{
    PyObject *key;
    PyObject *def = NULL;

    if (!PyArg_UnpackTuple(args, "get", 1, 2, &key, &def)) {
        return NULL;
    }

    return hamt_get(self, key, def);
}

static PyObject *
hamt_py_delete(PyHamtObject *self, PyObject *key)
{
    return (PyObject *)hamt_without(self, key);
}

static PyObject *
hamt_py_items(PyHamtObject *self, PyObject *args)
{
    return hamt_iter_new_items(self);
}

static PyObject *
hamt_py_values(PyHamtObject *self, PyObject *args)
{
    return hamt_iter_new_values(self);
}

static PyObject *
hamt_py_keys(PyHamtObject *self, PyObject *args)
{
    return hamt_iter_new_keys(self);
}

#ifdef Py_DEBUG
static PyObject *
hamt_py_dump(PyHamtObject *self, PyObject *args)
{
    return hamt_dump(self);
}
#endif


static PyMethodDef PyHamt_methods[] = {
    {"set", (PyCFunction)hamt_py_set, METH_VARARGS, NULL},
    {"get", (PyCFunction)hamt_py_get, METH_VARARGS, NULL},
    {"delete", (PyCFunction)hamt_py_delete, METH_O, NULL},
    {"items", (PyCFunction)hamt_py_items, METH_NOARGS, NULL},
    {"keys", (PyCFunction)hamt_py_keys, METH_NOARGS, NULL},
    {"values", (PyCFunction)hamt_py_values, METH_NOARGS, NULL},
#ifdef Py_DEBUG
    {"__dump__", (PyCFunction)hamt_py_dump, METH_NOARGS, NULL},
#endif
    {NULL, NULL}
};

static PySequenceMethods PyHamt_as_sequence = {
    0,                                /* sq_length */
    0,                                /* sq_concat */
    0,                                /* sq_repeat */
    0,                                /* sq_item */
    0,                                /* sq_slice */
    0,                                /* sq_ass_item */
    0,                                /* sq_ass_slice */
    (objobjproc)hamt_tp_contains,     /* sq_contains */
    0,                                /* sq_inplace_concat */
    0,                                /* sq_inplace_repeat */
};

static PyMappingMethods PyHamt_as_mapping = {
    (lenfunc)hamt_tp_len,             /* mp_length */
    (binaryfunc)hamt_tp_subscript,    /* mp_subscript */
};

PyTypeObject _PyHamt_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt",
    sizeof(PyHamtObject),
    .tp_methods = PyHamt_methods,
    .tp_as_mapping = &PyHamt_as_mapping,
    .tp_as_sequence = &PyHamt_as_sequence,
    .tp_iter = (getiterfunc)hamt_tp_iter,
    .tp_dealloc = (destructor)hamt_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_richcompare = hamt_tp_richcompare,
    .tp_traverse = (traverseproc)hamt_tp_traverse,
    .tp_clear = (inquiry)hamt_tp_clear,
    .tp_new = hamt_tp_new,
    .tp_weaklistoffset = offsetof(PyHamtObject, h_weakreflist),
    .tp_hash = PyObject_HashNotImplemented,
};


/////////////////////////////////// Tree Node Types


PyTypeObject _PyHamt_ArrayNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_array_node",
    sizeof(PyHamtNode_Array),
    0,
    .tp_dealloc = (destructor)hamt_node_array_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_array_traverse,
    .tp_free = PyObject_GC_Del,
    .tp_hash = PyObject_HashNotImplemented,
};

PyTypeObject _PyHamt_BitmapNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_bitmap_node",
    sizeof(PyHamtNode_Bitmap) - sizeof(PyObject *),
    sizeof(PyObject *),
    .tp_dealloc = (destructor)hamt_node_bitmap_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_bitmap_traverse,
    .tp_free = PyObject_GC_Del,
    .tp_hash = PyObject_HashNotImplemented,
};

PyTypeObject _PyHamt_CollisionNode_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "hamt_collision_node",
    sizeof(PyHamtNode_Collision) - sizeof(PyObject *),
    sizeof(PyObject *),
    .tp_dealloc = (destructor)hamt_node_collision_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)hamt_node_collision_traverse,
    .tp_free = PyObject_GC_Del,
    .tp_hash = PyObject_HashNotImplemented,
};
