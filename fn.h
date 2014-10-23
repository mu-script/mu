/*
 * First class function variable type
 */

#ifndef MU_FN_H
#define MU_FN_H

#include "var.h"
#include "tbl.h"


struct fn {
    const str_t *bcode; // function bytecode

    len_t stack;    // amount of stack usage
    len_t bcount;   // length of bytecode
    len_t fcount;   // number of stored functions
    len_t vcount;   // number of stored vars

    fn_t **fns;      // nested functions
    var_t *vars;    // stored vars
};

// Functions for managing functions
// Each function is preceded with a reference count
// which is used as its handle in a var
fn_t *fn_create(tbl_t *args, var_t code, eh_t *eh);
fn_t *fn_create_nested(tbl_t *args, void *vs, eh_t *eh);

// Called by garbage collector to clean up
void fn_destroy(void *);


// Call a function. Each function takes a table
// of arguments, and returns a single variable.
var_t fn_call(fn_t *, tbl_t *args, tbl_t *scope, eh_t *eh);


// Returns a string representation of a function
var_t fn_repr(var_t v, eh_t *eh);


// Function reference counting
static inline void fn_inc(void *m) { ref_inc(m); }
static inline void fn_dec(void *m) { ref_dec(m, fn_destroy); }


#endif
