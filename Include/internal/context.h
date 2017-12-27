#ifndef Py_INTERNAL_CONTEXT_H
#define Py_INTERNAL_CONTEXT_H


#include "internal/hamt.h"


struct _pycontextobject {
    PyObject_HEAD
    PyHamtObject *ctx_prev;
    PyHamtObject *ctx_vars;
    PyObject *ctx_weakreflist;
    int ctx_prev_set;
};


struct _pycontextvarobject {
    PyObject_HEAD
    PyObject *var_name;
    PyObject *var_default;
    PyObject *var_cached;
    uint64_t var_cached_tsid;
    uint64_t var_cached_tsver;
    Py_hash_t var_hash;
};


struct _pycontexttokenobject {
    PyObject_HEAD
    PyContextVar *tok_var;
    PyObject *tok_oldval;
    int tok_used;
};


int _PyContext_Init(void);
void _PyContext_Fini(void);


#endif /* !Py_INTERNAL_CONTEXT_H */
