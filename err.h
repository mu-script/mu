/*
 * Error Handling
 */

#ifndef MU_ERR_H
#define MU_ERR_H
#include "mu.h"
#include "types.h"
#include <setjmp.h>


// Definition of overhead for error handler in Mu
struct eh {
    struct eh *prev;
    mu_t handles;
    jmp_buf env;
};


struct eh *eh_get(void);
void eh_set(struct eh *eh);

void eh_handle(struct eh *eh, mu_t err);


mu_noreturn mu_err(mu_t err);
mu_noreturn mu_cerr(mu_t type, mu_t reason);

mu_noreturn mu_err_nomem(void);
mu_noreturn mu_err_len(void);
mu_noreturn mu_err_readonly(void);
mu_noreturn mu_err_parse(void);
mu_noreturn mu_err_undefined(void);


#define mu_try_begin {                      \
    struct eh _eh = {eh_get(), 0};          \
    mu_t _err = (mu_t)setjmp(_eh.env);      \
                                            \
    if (mu_unlikely(_err))                  \
        eh_handle(&_eh, _err);              \
                                            \
    if (mu_likely(!_err)) {                 \
        eh_set(&_eh);                       \
{
#define mu_on_err(err)                      \
}                                           \
    } else {                                \
        mu_unused mu_t err = _err;          \
        eh_set(_eh.prev);                   \
{
#define mu_try_end                          \
}                                           \
    }                                       \
                                            \
    eh_set(_eh.prev);                       \
}


#endif