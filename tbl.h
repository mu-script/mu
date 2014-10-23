/*
 * Table variable type
 */

#ifndef MU_TBL_H
#define MU_TBL_H

#include "var.h"
#include "num.h"

#include <assert.h>


// Each table is composed of an array of values 
// with a stride for keys/values. If keys/values 
// is not stored in the array it is implicitely 
// stored as a range/offset based on the specified 
// offset and length.
struct tbl {
    struct tbl *tail; // tail chain of tables

    len_t nils;     // count of nil entries
    len_t len;      // count of keys in use
    hash_t mask;    // size of entries - 1

    enum { 
        TBL_RANGE = 0, 
        TBL_LIST  = 1, 
        TBL_HASH  = 2 
    } stride;           // table types

    union {
        int offset;     // offset for implicit ranges
        var_t *array;   // pointer to stored data
    };
};


// Functions for managing tables
// Each table is preceeded with a reference count
// which is used as its handle in a var
tbl_t *tbl_create(len_t size, eh_t *eh);

// Called by garbage collector to clean up
void tbl_destroy(void *);


// Recursively looks up a key in the table
// returns either that value or nil
var_t tbl_lookup(tbl_t *, var_t key);

// Recursively looks up either a key or index
// if key is not found
var_t tbl_lookdn(tbl_t *, var_t key, len_t i);

// Recursively assigns a value in the table with the given key
// decends down the tail chain until its found
void tbl_assign(tbl_t *, var_t key, var_t val, eh_t *eh);

// Inserts a value in the table with the given key
// without decending down the tail chain
void tbl_insert(tbl_t *, var_t key, var_t val, eh_t *eh);

// Sets the next index in the table with the value
void tbl_append(tbl_t *, var_t val, eh_t *eh);


// Performs iteration on a table
var_t tbl_iter(var_t v, eh_t *eh);

// Returns a string representation of the table
var_t tbl_repr(var_t v, eh_t *eh);



// Macro for iterating through a table in c
// Assign names for k and v, and pass in the 
// block to execute for each pair in tbl
#define tbl_for_begin(k, v, tbl) {                  \
    var_t k;                                        \
    var_t v;                                        \
    tbl_t *_t = tbl_readp(tbl);                     \
    int _i, _c = _t->len;                           \
                                                    \
    for (_i=0; _c; _i++) {                          \
        switch (_t->stride) {                       \
            case 0:                                 \
                k = vnum(_i);                       \
                v = vnum(_t->offset + _i);          \
                break;                              \
            case 1:                                 \
                k = vnum(_i);                       \
                v = _t->array[_i];                  \
                break;                              \
            case 2:                                 \
                k = _t->array[2*_i  ];              \
                v = _t->array[2*_i+1];              \
                if (isnil(k) || isnil(v))           \
                    continue;                       \
                break;                              \
        }                                           \
{
#define tbl_for_end                                 \
}                                                   \
        _c--;                                       \
    }                                               \
}


// Accessing table pointers with the ro flag
static inline bool tbl_isro(tbl_t *tbl) {
    return 0x1 & (uint32_t)tbl;
}

static inline tbl_t *tbl_ro(tbl_t *tbl) {
    return (tbl_t *)(0x1 | (uint32_t)tbl);
}

static inline tbl_t *tbl_readp(tbl_t *tbl) {
    return (tbl_t *)(~0x1 & (uint32_t)tbl);
}

static inline tbl_t *tbl_writep(tbl_t *tbl, eh_t *eh) {
    if (tbl_isro(tbl))
        err_ro(eh);

    return tbl;
}


// Table reference counting
static inline void tbl_inc(void *m) { ref_inc(m); }
static inline void tbl_dec(void *m) { ref_dec(m, tbl_destroy); }


#endif
