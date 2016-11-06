#include "str.h"

#include "buf.h"
#include "num.h"
#include "tbl.h"
#include "fn.h"
#include "parse.h"


#define MU_EMPTY_STR mu_empty_str()
#define MU_SPACE_STR mu_space_str()
MSTR(mu_empty_str, "")
MSTR(mu_space_str, " ")


// String interning
//
// Currently interning is implemented using a sorted array with a
// binary search for insertion. This was chosen for a few reasons:
// - We can't reuse the table's implementation since it relies
//   on interned strings.
// - Lower memory overhead/more cache friendly than tree
//   or trie implementation.
// - Strings don't need to be hashed, its possible strings won't
//   even need to be completely scanned during comparisons on
//   lookup/insertion.
static mu_t *str_table = 0;
static muint_t str_table_size = 0;
static muint_t str_table_len = 0;

static mint_t str_table_find(const mbyte_t *s, mlen_t len) {
    mint_t min = 0;
    mint_t max = str_table_len-1;

    // binary search for existing strings
    // strings are sorted first by length to avoid comparisons
    while (min <= max) {
        mint_t mid = (max + min) / 2;
        mint_t cmp = len > str_len(str_table[mid]) ? +1 :
                     len < str_len(str_table[mid]) ? -1 :
                     memcmp(s, str_data(str_table[mid]), len);

        if (cmp == 0) {
            return mid;
        } else if (cmp < 0) {
            max = mid-1;
        } else {
            min = mid+1;
        }
    }

    // Use inverted values to indicate not found but where to
    // insert since 0 is valid in both cases
    return ~min;
}

static void str_table_insert(mint_t i, mu_t s) {
    // expand the table if necessary
    if (str_table_len == str_table_size) {
        muint_t nsize;
        mu_t *ntable;

        if (str_table_size == 0) {
            nsize = MU_MINALLOC / sizeof(mu_t);
        } else {
            nsize = str_table_size << 1;
        }

        ntable = mu_alloc(nsize * sizeof(mu_t));
        memcpy(ntable, str_table, i * sizeof(mu_t));
        memcpy(&ntable[i+1], &str_table[i],
               (str_table_len-i) * sizeof(mu_t));
        mu_dealloc(str_table, str_table_size * sizeof(mu_t));

        str_table = ntable;
        str_table_size = nsize;
    } else {
        memmove(&str_table[i+1], &str_table[i],
                (str_table_len-i) * sizeof(mu_t));
    }

    str_table[i] = s;
    str_table_len += 1;
}

static void str_table_remove(mint_t i) {
    str_table_len -= 1;
    memmove(&str_table[i], &str_table[i+1],
            (str_table_len-i) * sizeof(mu_t));
}


// String management
// This can avoid unnecessary allocations sometimes since
// buf's internal structure is reused for interned strings
mu_t str_intern(mu_t b, muint_t n) {
    mu_assert(mu_isbuf(b));

    mint_t i = str_table_find(buf_data(b), n);
    if (i >= 0) {
        buf_dec(b);
        return str_inc(str_table[i]);
    }

    if (buf_dtor(b)) {
        buf_setdtor(&b, 0);
    }

    if (buf_len(b) != n) {
        buf_resize(&b, n);
    }

    mu_t s = (mu_t)((muint_t)b - MTBUF + MTSTR);
    str_table_insert(~i, s);
    return str_inc(s);
}

mu_t str_create(const mbyte_t *s, muint_t n) {
    if (n > (mlen_t)-1) {
        mu_errorf("exceeded max length in string");
    }

    mint_t i = str_table_find(s, n);
    if (i >= 0) {
        return str_inc(str_table[i]);
    }

    // create new string and insert
    mu_t b = buf_create(n);
    memcpy(buf_data(b), s, n);

    mu_t ns = (mu_t)((muint_t)b - MTBUF + MTSTR);
    str_table_insert(~i, ns);
    return str_inc(ns);
}

void str_destroy(mu_t s) {
    mint_t i = str_table_find(str_data(s), str_len(s));
    mu_assert(i >= 0);
    str_table_remove(i);

    ref_dealloc(s, sizeof(struct str) + str_len(s));
}


// String creating functions
mu_t str_init(const struct str *s) {
    mu_t m = str_intern((mu_t)((muint_t)s + MTBUF), s->len);

    if (*(mref_t *)((muint_t)m - MTSTR) != 0) {
        *(mref_t *)((muint_t)m - MTSTR) = 0;
    }

    return m;
}

mu_t str_fromnum(mu_t n) {
    mu_assert(n == muint((mbyte_t)num_uint(n)));
    return str_create((mbyte_t[]){num_uint(n)}, 1);
}

mu_t str_fromiter(mu_t iter) {
    return fn_call(MU_JOIN, 0x21, iter, MU_EMPTY_STR);
}

mu_t str_vformat(const char *f, va_list args) {
    mu_t b = buf_create(0);
    muint_t n = 0;
    buf_vformat(&b, &n, f, args);
    return str_intern(b, n);
}

mu_t str_format(const char *f, ...) {
    va_list args;
    va_start(args, f);
    mu_t m = str_vformat(f, args);
    va_end(args);
    return m;
}


// Comparison operation
mint_t str_cmp(mu_t a, mu_t b) {
    mu_assert(mu_isstr(a) && mu_isstr(b));

    if (a == b) {
        return 0;
    }

    muint_t alen = str_len(a);
    muint_t blen = str_len(b);
    mint_t cmp = memcmp(str_data(a), str_data(b), 
                        alen < blen ? alen : blen);

    return cmp != 0 ? cmp : alen - blen;
}


// String iteration
bool str_next(mu_t s, muint_t *ip, mu_t *cp) {
    mu_assert(mu_isstr(s));
    muint_t i = *ip;

    if (i >= str_len(s)) {
        return false;
    }

    if (cp) *cp = str_create(&str_data(s)[i], 1);
    *ip = i + 1;
    return true;
}

static mcnt_t str_step(mu_t scope, mu_t *frame) {
    mu_t s = tbl_lookup(scope, muint(0));
    muint_t i = num_uint(tbl_lookup(scope, muint(1)));

    bool next = str_next(s, &i, &frame[0]);
    str_dec(s);
    tbl_insert(scope, muint(1), muint(i));
    return next ? 1 : 0;
}

mu_t str_iter(mu_t s) {
    mu_assert(mu_isstr(s));
    return msbfn(0x00, str_step, mlist({s, muint(0)}));
}


// String operations
mu_t str_concat(mu_t a, mu_t b) {
    mu_assert(mu_isstr(a) && mu_isstr(b));
    muint_t an = str_len(a);
    muint_t bn = str_len(b);
    mu_t d = buf_create(an + bn);

    memcpy((mbyte_t *)buf_data(d), str_data(a), an);
    memcpy((mbyte_t *)buf_data(d)+an, str_data(b), bn);

    str_dec(a);
    str_dec(b);
    return str_intern(d, an + bn);
}

mu_t str_subset(mu_t s, mint_t lower, mint_t upper) {
    mu_assert(mu_isstr(s));
    lower = (lower >= 0) ? lower : lower + str_len(s);
    upper = (upper >= 0) ? upper : upper + str_len(s);

    if (lower < 0) {
        lower = 0;
    }

    if (upper > str_len(s)) {
        upper = str_len(s);
    }

    if (lower >= upper) {
        return MU_EMPTY_STR;
    }

    mu_t d = str_create(str_data(s) + lower, upper - lower);
    str_dec(s);
    return d;
}


// String representation
mu_t str_parse(const mbyte_t **ppos, const mbyte_t *end) {
    const mbyte_t *pos = *ppos;
    mbyte_t quote = *pos++;
    mu_t b = buf_create(0);
    muint_t n = 0;

    while (pos < end-1 && *pos != quote) {
        if (*pos == '\\') {
            if (pos[1] == 'b' && pos < end-9 &&
                (mu_fromascii(pos[2]) < 2 &&
                 mu_fromascii(pos[3]) < 2 &&
                 mu_fromascii(pos[4]) < 2 &&
                 mu_fromascii(pos[5]) < 2 &&
                 mu_fromascii(pos[6]) < 2 &&
                 mu_fromascii(pos[7]) < 2 &&
                 mu_fromascii(pos[8]) < 2 &&
                 mu_fromascii(pos[9]) < 2)) {
                buf_push(&b, &n,
                        mu_fromascii(pos[2])*2*2*2*2*2*2*2 +
                        mu_fromascii(pos[3])*2*2*2*2*2*2 +
                        mu_fromascii(pos[4])*2*2*2*2*2 +
                        mu_fromascii(pos[5])*2*2*2*2 +
                        mu_fromascii(pos[6])*2*2*2 +
                        mu_fromascii(pos[7])*2*2 +
                        mu_fromascii(pos[8])*2 +
                        mu_fromascii(pos[9]));
                pos += 10;
            } else if (pos[1] == 'o' && pos < end-4 &&
                (mu_fromascii(pos[2]) < 8 &&
                 mu_fromascii(pos[3]) < 8 &&
                 mu_fromascii(pos[4]) < 8)) {
                buf_push(&b, &n,
                        mu_fromascii(pos[2])*8*8 +
                        mu_fromascii(pos[3])*8 +
                        mu_fromascii(pos[4]));
                pos += 5;
            } else if (pos[1] == 'd' && pos < end-4 &&
                       (mu_fromascii(pos[2]) < 10 &&
                        mu_fromascii(pos[3]) < 10 &&
                        mu_fromascii(pos[4]) < 10)) {
                buf_push(&b, &n,
                        mu_fromascii(pos[2])*10*10 +
                        mu_fromascii(pos[3])*10 +
                        mu_fromascii(pos[4]));
                pos += 5;
            } else if (pos[1] == 'x' && pos < end-3 &&
                       (mu_fromascii(pos[2]) < 16 &&
                        mu_fromascii(pos[3]) < 16)) {
                buf_push(&b, &n,
                        mu_fromascii(pos[2])*16 +
                        mu_fromascii(pos[3]));
                pos += 4;
            } else if (pos[1] == '\\') {
                buf_push(&b, &n, '\\'); pos += 2;
            } else if (pos[1] == '\'') {
                buf_push(&b, &n, '\''); pos += 2;
            } else if (pos[1] == '\"') {
                buf_push(&b, &n, '\"'); pos += 2;
            } else if (pos[1] == 'f') {
                buf_push(&b, &n, '\f'); pos += 2;
            } else if (pos[1] == 'n') {
                buf_push(&b, &n, '\n'); pos += 2;
            } else if (pos[1] == 'r') {
                buf_push(&b, &n, '\r'); pos += 2;
            } else if (pos[1] == 't') {
                buf_push(&b, &n, '\t'); pos += 2;
            } else if (pos[1] == 'v') {
                buf_push(&b, &n, '\v'); pos += 2;
            } else if (pos[1] == '0') {
                buf_push(&b, &n, '\0'); pos += 2;
            } else {
                buf_push(&b, &n, '\\'); pos += 1;
            }
        } else {
            buf_push(&b, &n, *pos++);
        }
    }

    if (quote != *pos++) {
        mu_errorf("unterminated string literal");
    }

    *ppos = pos;
    return str_intern(b, n);
}

// Returns a string representation of a string
mu_t str_repr(mu_t m) {
    mu_assert(mu_isstr(m));
    const mbyte_t *pos = str_data(m);
    const mbyte_t *end = pos + str_len(m);
    mu_t b = buf_create(2 + str_len(m));
    muint_t n = 0;

    buf_push(&b, &n, '\'');

    for (; pos < end; pos++) {
        if (*pos < ' ' || *pos > '~' ||
            *pos == '\\' || *pos == '\'') {
            if (*pos == '\\')      buf_format(&b, &n, "\\\\");
            else if (*pos == '\'') buf_format(&b, &n, "\\'");
            else if (*pos == '\f') buf_format(&b, &n, "\\f");
            else if (*pos == '\n') buf_format(&b, &n, "\\n");
            else if (*pos == '\r') buf_format(&b, &n, "\\r");
            else if (*pos == '\t') buf_format(&b, &n, "\\t");
            else if (*pos == '\v') buf_format(&b, &n, "\\v");
            else if (*pos == '\0') buf_format(&b, &n, "\\0");
            else                   buf_format(&b, &n, "\\x%bx", *pos);
        } else {
            buf_push(&b, &n, *pos);
        }
    }

    str_dec(m);
    buf_push(&b, &n, '\'');
    return str_intern(b, n);
}

static mu_t str_base(mu_t m, char b, muint_t size, muint_t mask, muint_t shift) {
    const mbyte_t *s = str_data(m);
    mlen_t len = str_len(m);

    mu_t buf = buf_create(2 + (2+size)*len);
    mbyte_t *d = buf_data(buf);
    d[0] = '\'';

    for (muint_t i = 0; i < len; i++) {
        mbyte_t c = s[i];
        d[1 + (2+size)*i+0] = '\\';
        d[1 + (2+size)*i+1] = b;

        for (muint_t j = 0; j < size; j++) {
            d[1 + (2+size)*i+2 + (size-1-j)] = mu_toascii(c & mask);
            c >>= shift;
        }
    }

    d[1 + (2+size)*len] = '\'';
    str_dec(m);
    return str_intern(buf, 2 + (2+size)*len);
}

mu_t str_bin(mu_t s) {
    mu_assert(mu_isstr(s));
    return str_base(s, 'b', 8,   1, 1);
}

mu_t str_oct(mu_t s) {
    mu_assert(mu_isstr(s));
    return str_base(s, 'o', 3,   7, 3);
}

mu_t str_hex(mu_t s) {
    mu_assert(mu_isstr(s));
    return str_base(s, 'x', 2, 0xf, 4);
}


// String related functions in Mu
static mcnt_t mu_bfn_str(mu_t *frame) {
    mu_t m = frame[0];

    switch (mu_type(m)) {
        case MTNIL:
            frame[0] = MU_EMPTY_STR;
            return 1;

        case MTNUM:
            if (m == muint((mbyte_t)num_uint(m))) {
                frame[0] = str_fromnum(m);
                return 1;
            }
            break;

        case MTSTR:
            frame[0] = frame[0];
            return 1;

        case MTTBL:
        case MTFN:
            frame[0] = m;
            fn_fcall(MU_ITER, 0x11, frame);
            frame[0] = str_fromiter(frame[0]);
            return 1;

        default:
            break;
    }

    mu_error_cast(MU_KEY_STR, m);
}

MSTR(mu_gen_key_str, "str")
MBFN(mu_gen_str, 0x1, mu_bfn_str)

static mcnt_t mu_bfn_find(mu_t *frame) {
    mu_t s = frame[0];
    mu_t m = frame[1];
    if (!mu_isstr(s) || !mu_isstr(m)) {
        mu_error_arg(MU_KEY_FIND, 0x2, frame);
    }

    const mbyte_t *sb = str_data(s);
    mlen_t slen = str_len(s);
    const mbyte_t *mb = str_data(m);
    mlen_t mlen = str_len(m);

    for (muint_t i = 0; i+mlen <= slen; i++) {
        if (memcmp(&sb[i], mb, mlen) == 0) {
            str_dec(m);
            str_dec(s);
            frame[0] = muint(i);
            frame[1] = muint(i + mlen);
            return 2;
        }
    }

    str_dec(s);
    str_dec(m);
    return 0;
}

MSTR(mu_gen_key_find, "find")
MBFN(mu_gen_find, 0x2, mu_bfn_find)

static mcnt_t mu_bfn_replace(mu_t *frame) {
    mu_t s = frame[0];
    mu_t m = frame[1];
    mu_t r = frame[2];
    if (!mu_isstr(s) || !mu_isstr(m) || !mu_isstr(r)) {
        mu_error_arg(MU_KEY_REPLACE, 0x3, frame);
    }

    const mbyte_t *sb = str_data(s);
    mlen_t slen = str_len(s);
    const mbyte_t *mb = str_data(m);
    mlen_t mlen = str_len(m);

    mu_t d = buf_create(slen);
    muint_t n = 0;
    muint_t i = 0;

    while (i+mlen <= slen) {
        bool match = memcmp(&sb[i], mb, mlen) == 0;

        if (match) {
            buf_concat(&d, &n, str_inc(r));
            i += mlen;
        }

        if (!match || mlen == 0) {
            buf_push(&d, &n, sb[i]);
            i += 1;
        }
    }

    buf_append(&d, &n, &sb[i], slen-i);

    str_dec(s);
    str_dec(m);
    str_dec(r);
    frame[0] = str_intern(d, n);
    return 1;
}

MSTR(mu_gen_key_replace, "replace")
MBFN(mu_gen_replace, 0x3, mu_bfn_replace)


static mcnt_t str_split_step(mu_t scope, mu_t *frame) {
    mu_t a = tbl_lookup(scope, muint(0));
    const mbyte_t *ab = str_data(a);
    mlen_t alen = str_len(a);
    muint_t i = num_uint(tbl_lookup(scope, muint(2)));

    if (i > alen) {
        str_dec(a);
        return 0;
    }

    mu_t s = tbl_lookup(scope, muint(1));
    const mbyte_t *sb = str_data(s);
    mlen_t slen = str_len(s);

    muint_t j = i;
    for (; j < alen; j++) {
        if (j+slen <= alen && memcmp(&ab[j], sb, slen) == 0) {
            break;
        }
    }

    frame[0] = str_create(ab+i, j-i);
    tbl_insert(scope, muint(2), muint(j+slen));
    str_dec(a);
    str_dec(s);
    return 1;
}

static mcnt_t mu_bfn_split(mu_t *frame) {
    mu_t s     = frame[0];
    mu_t delim = frame[1] ? frame[1] : MU_SPACE_STR;
    if (!mu_isstr(s) || !mu_isstr(delim)) {
        mu_error_arg(MU_KEY_SPLIT, 0x2, frame);
    }

    if (str_len(delim) == 0) {
        frame[0] = str_iter(s);
        return 1;
    }

    frame[0] = msbfn(0x0, str_split_step, mlist({s, delim, muint(0)}));
    return 1;
}

MSTR(mu_gen_key_split, "split")
MBFN(mu_gen_split, 0x2, mu_bfn_split)

static mcnt_t mu_bfn_join(mu_t *frame) {
    mu_t iter  = frame[0];
    mu_t delim = frame[1] ? frame[1] : MU_SPACE_STR;
    if (!mu_isstr(delim)) {
        mu_error_arg(MU_KEY_JOIN, 0x2, frame);
    }

    mu_t b = buf_create(0);
    muint_t n = 0;
    bool first = true;

    iter = fn_call(MU_ITER, 0x11, iter);

    while (fn_next(iter, 0x1, frame)) {
        if (!mu_isstr(frame[0])) {
            mu_errorf("invalid value %r passed to join", frame[0]);
        }

        if (first) {
            first = false;
        } else {
            buf_concat(&b, &n, str_inc(delim));
        }

        buf_concat(&b, &n, frame[0]);
    }

    fn_dec(iter);
    str_dec(delim);
    frame[0] = str_intern(b, n);
    return 1;
}

MSTR(mu_gen_key_join, "join")
MBFN(mu_gen_join, 0x2, mu_bfn_join)

static mcnt_t mu_bfn_pad(mu_t *frame) {
    mu_t s    = frame[0];
    mu_t mlen = frame[1];
    mu_t pad  = frame[2] ? frame[2] : MU_SPACE_STR;
    if (!mu_isstr(s) ||
        !mu_isnum(mlen) ||
        (!mu_isstr(pad) || str_len(pad) == 0)) {
        mu_error_arg(MU_KEY_PAD, 0x3, frame);
    }

    bool left;
    muint_t len;

    if (num_cmp(mlen, muint(0)) < 0) {
        left = false;
        len = num_uint(num_neg(mlen));
    } else {
        left = true;
        len = num_uint(mlen);
    }

    if (str_len(s) >= len) {
        str_dec(pad);
        return 1;
    }

    mu_t d = buf_create(len);
    muint_t n = 0;
    muint_t count = (len - str_len(s)) / str_len(pad);

    if (left) {
        buf_concat(&d, &n, s);
    }

    for (muint_t i = 0; i < count; i++) {
        buf_concat(&d, &n, str_inc(pad));
    }
    
    if (!left) {
        buf_concat(&d, &n, s);
    }

    mu_dec(pad);
    frame[0] = str_intern(d, n);
    return 1;
}

MSTR(mu_gen_key_pad, "pad")
MBFN(mu_gen_pad, 0x3, mu_bfn_pad)

static mcnt_t mu_bfn_strip(mu_t *frame) {
    if (mu_isstr(frame[1])) {
        mu_dec(frame[2]);
        frame[2] = frame[1];
        frame[1] = 0;
    }

    mu_t s   = frame[0];
    mu_t dir = frame[1];
    mu_t pad = frame[2] ? frame[2] : MU_SPACE_STR;
    if (!mu_isstr(s) ||
        (dir && !mu_isnum(dir)) ||
        (!mu_isstr(pad) || str_len(pad) == 0)) {
        mu_error_arg(MU_KEY_STR, 0x3, frame);
    }

    const mbyte_t *pos = str_data(s);
    const mbyte_t *end = pos + str_len(s);

    const mbyte_t *pb = str_data(pad);
    mlen_t plen = str_len(pad);

    if (!dir || num_cmp(dir, muint(0)) <= 0) {
        while (end-pos >= plen && memcmp(pos, pb, plen) == 0) {
            pos += plen;
        }
    }
            
    if (!dir || num_cmp(dir, muint(0)) >= 0) {
        while (end-pos >= plen && memcmp(end-plen, pb, plen) == 0) {
            end -= plen;
        }
    }

    frame[0] = str_create(pos, end-pos);
    str_dec(s);
    str_dec(pad);
    return 1;
}

MSTR(mu_gen_key_strip, "strip")
MBFN(mu_gen_strip, 0x3, mu_bfn_strip)
