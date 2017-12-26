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
    Py_hash_t var_hash;
};


struct _pycontexttokenobject {
    PyObject_HEAD
    PyContextVar *tok_var;
    PyObject *tok_oldval;
    int tok_used;
};


#endif /* !Py_INTERNAL_CONTEXT_H */
