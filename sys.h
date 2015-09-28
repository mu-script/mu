/*
 * System dependent implementation
 */

#ifndef MU_SYS_H
#define MU_SYS_H
#include "mu.h"


// It's up to the system using Mu to provide the following functions

// Called when an error occurs, it's up to the system what to do,
// but this function can not return.
mu_noreturn sys_error(const char *message, muint_t len);

// Called by Mu's print function. Intended for debugging purposes.
void sys_print(const char *message, muint_t len);

// Called by Mu to import a module if it can't be found in the
// currently loaded modules.
mu_t sys_import(mu_t name);


// These can be defined but are only used if MU_MALLOC is undefined

// Allocate memory
// Must garuntee 8-byte alignment
void *sys_alloc(size_t size);

// Deallocate memory
// Must not error on 0
void sys_dealloc(void *m, size_t size);


#endif
