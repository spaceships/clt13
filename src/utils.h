#pragma once

#include <assert.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdio.h>

#pragma GCC visibility push(hidden)

double current_time(void);
void print_progress(size_t cur, size_t total);

/* `rop` = `a` mod `p`, where -p/2 < rop < p/2 */
static inline void
mpz_mod_near(mpz_t rop, const mpz_t a, const mpz_t p)
{
    mpz_t p_;
    mpz_init(p_);
    mpz_mod(rop, a, p);
    mpz_cdiv_q_ui(p_, p, 2);
    if (mpz_cmp(rop, p_) > 0)
        mpz_sub(rop, rop, p);
    mpz_clear(p_);
}

static inline void
mpz_mul_mod(mpz_t rop, mpz_t a, const mpz_t b, const mpz_t p)
{
    mpz_mul(rop, a, b);
    mpz_mod_near(rop, rop, p);
}

static inline void
mpz_random_(mpz_t rop, aes_randstate_t rng, size_t len)
{
    mpz_urandomb_aes(rop, rng, len);
    mpz_setbit(rop, len-1);
}

static inline void
mpz_prime(mpz_t rop, aes_randstate_t rng, size_t len)
{
    mpz_t p_unif;
    mpz_init(p_unif);
    do {
        mpz_random_(p_unif, rng, len);
        mpz_nextprime(rop, p_unif);
    } while (mpz_tstbit(rop, len) == 1);
    assert(mpz_tstbit(rop, len-1) == 1);
    mpz_clear(p_unif);
}

static inline void
product(mpz_t rop, mpz_t *xs, size_t n, bool verbose)
{
    double start = current_time();
    /* TODO: could parallelize this if desired */
    mpz_set_ui(rop, 1);
    for (size_t i = 0; i < n; i++) {
        mpz_mul(rop, rop, xs[i]);
        if (verbose)
            print_progress(i, n-1);
    }
    if (verbose)
        fprintf(stderr, "\t[%.2fs]\n", current_time() - start);
}


static inline void
crt_coeffs(mpz_t *coeffs, mpz_t *ps, size_t n, mpz_t x0, bool verbose)
{
    const double start = current_time();
    int count = 0;
    if (verbose)
        fprintf(stderr, "  Generating CRT coefficients:\n");
#pragma omp parallel for
    for (size_t i = 0; i < n; i++) {
        mpz_t q;
        mpz_init(q);
        mpz_div(q, x0, ps[i]);
        mpz_invert(coeffs[i], q, ps[i]);
        mpz_mul_mod(coeffs[i], coeffs[i], q, x0);
        mpz_clear(q);
        if (verbose) {
#pragma omp critical
            print_progress(++count, n);
        }
    }
    if (verbose)
        fprintf(stderr, "\t[%.2fs]\n", current_time() - start);
}

int mpz_fread(mpz_t x, FILE *fp);
int mpz_fwrite(mpz_t x, FILE *fp);
mpz_t * mpz_vector_new(size_t n);
void mpz_vector_free(mpz_t *v, size_t n);
int mpz_vector_fread(mpz_t *m, size_t len, FILE *fp);
int mpz_vector_fwrite(mpz_t *m, size_t len, FILE *fp);

int size_t_fread(FILE *fp, size_t *x);
int size_t_fwrite(FILE *fp, size_t x);

#pragma GCC visibility pop
