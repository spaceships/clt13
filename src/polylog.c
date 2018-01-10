#include "polylog.h"

#include "_clt13.h"
#include "utils.h"

#include <gmp.h>
#include <stdio.h>
#include <stdlib.h>

static switch_state_t *
switch_state_new(clt_state_t *s, size_t wordsize, size_t level, bool verbose)
{
    polylog_state_t *pstate = s->pstate;
    switch_state_t *state;
    mpz_t K, *ginvs, *fs, **ss;

    if (level >= pstate->nlevels - 1) {
        fprintf(stderr, "error: level too large (%lu ≥ %lu)\n", level, pstate->nlevels - 1);
        return NULL;
    }

    if ((state = calloc(1, sizeof state[0])) == NULL)
        return NULL;
    state->level = level;
    state->wordsize = wordsize;
    /* k = η_ℓ / log2(wordsize) */
    state->k = (int) ceil(pstate->etas[level] / log2(wordsize));

    if (verbose)
        fprintf(stderr, "  Generating switch state [%lu->%lu]\n", level, level+1);

    /* K = Π^(ℓ) · (wordsize)ᵏ */
    mpz_init(K);
    mpz_ui_pow_ui(K, wordsize, state->k);
    mpz_mul(K, K, pstate->x0s[level]);

    /* Compute gᵢ^(-1) mod pᵢ^(ℓ) */
    ginvs = calloc(s->n, sizeof ginvs[0]);
    for (size_t i = 0; i < s->n; ++i) {
        mpz_init(ginvs[i]);
        mpz_invert(ginvs[i], s->gs[i], pstate->ps[level][i]);
    }
    /* Compute fᵢ' */
    fs = calloc(s->n, sizeof fs[0]);
    for (size_t i = 0; i < s->n; ++i) {
        mpz_init(fs[i]);
        mpz_mul(fs[i], ginvs[i], pstate->ps[level + 1][i]);
        mpz_fdiv_q(fs[i], fs[i], pstate->ps[level][i]);
        mpz_mul(fs[i], fs[i], s->gs[i]);
        mpz_mod_near(fs[i], fs[i], pstate->ps[level + 1][i]);
        mpz_mod_near(fs[i], fs[i], s->gs[i]);
        mpz_invert(fs[i], fs[i], s->gs[i]);
        mpz_mul(fs[i], fs[i], ginvs[i]);
    }

    state->ys = calloc(pstate->theta, sizeof state->ys[0]);
    for (size_t i = 0; i < pstate->theta; ++i)
        mpz_init(state->ys[i]);
    /* Sample y_{n+1}, ..., y_Θ ∈ [0, K) */
    for (size_t i = s->n; i < pstate->theta; ++i) {
        mpz_urandomm_aes(state->ys[i], s->rngs[0], K);
    }
    ss = calloc(s->n, sizeof ss[0]);
    for (size_t i = 0; i < s->n; ++i) {
        mpz_t tmp;

        mpz_init(tmp);

        ss[i] = calloc(pstate->theta, sizeof ss[i][0]);
        for (size_t j = 0; j <= s->n; ++j)
            mpz_init_set_ui(ss[i][j], 0);
        for (size_t j = s->n + 1; j < pstate->theta; ++j) {
            mpz_init(ss[i][j]);
            mpz_urandomb_aes(ss[i][j], s->rngs[0], wordsize);
        }
        mpz_set_ui(ss[i][i], 1);

        /* XXX multiply by z */
        mpz_mod_near(state->ys[i], fs[i], pstate->ps[level][i]);
        mpz_mul(state->ys[i], state->ys[i], K);
        mpz_fdiv_q(state->ys[i], state->ys[i], pstate->ps[level][i]);

        mpz_set_ui(tmp, 0);
        for (size_t j = s->n; j < pstate->theta; ++j) {
            mpz_t m;
            mpz_init(m);
            mpz_mul(m, state->ys[j], ss[i][j]);
            mpz_add(tmp, tmp, m);
            mpz_clear(m);
        }
        mpz_sub(state->ys[i], state->ys[i], tmp);
        mpz_mod_near(state->ys[i], state->ys[i], K);
        mpz_clear(tmp);
    }

    mpz_t wk;

    mpz_init(wk);
    mpz_ui_pow_ui(wk, wordsize, state->k);
    state->sigmas = calloc(pstate->theta, sizeof state->sigmas[0]);
    for (size_t t = 0; t < pstate->theta; ++t) {
        state->sigmas[t] = calloc(state->k, sizeof state->sigmas[t][0]);
        for (size_t j = 0; j < state->k; ++j) {
            mpz_t *xs = calloc(s->n, sizeof xs[0]);
            state->sigmas[t][j] = clt_elem_new();
            for (size_t i = 0; i < s->n; ++i) {
                mpz_init(xs[i]);
                mpz_ui_pow_ui(xs[i], wordsize, j);
                mpz_mul(xs[i], xs[i], ss[i][t]);
                mpz_mul(xs[i], xs[i], pstate->ps[level + 1][i]);
                mpz_fdiv_q(xs[i], xs[i], wk);
                mpz_mul(xs[i], xs[i], s->gs[i]);
            }
            polylog_encode(state->sigmas[t][j], s, s->n, xs, NULL, level + 1);
            for (size_t i = 0; i < s->n; ++i) {
                mpz_clear(xs[i]);
            }
            free(xs);
        }
    }

    return state;
}

void
polylog_state_free(polylog_state_t *state)
{
    if (state == NULL)
        return;
    if (state->etas)
        free(state->etas);
    for (size_t i = 0; i < state->nlevels; ++i) {
        for (size_t j = 0; j < state->n; ++j) {
            mpz_clear(state->ps[i][j]);
        }
        free(state->ps[i]);
    }
    free(state->ps);
    free(state);
}

polylog_state_t *
polylog_state_new(clt_state_t *s, size_t eta, size_t b, size_t nlevels,
                  size_t *levels, size_t nswitches)
{
    const bool verbose = s->flags & CLT_FLAG_VERBOSE;
    polylog_state_t *state;
    int count;

    if ((state = calloc(1, sizeof state[0])) == NULL)
        return NULL;
    s->pstate = state;
    state->n = s->n;
    state->b = b;
    state->nlevels = nlevels + 1;
    state->theta = 10;
    assert(state->theta > state->n);
    if (verbose) {
        fprintf(stderr, "Polylog CLT initialization:\n");
        fprintf(stderr, "  n: ..... %lu\n", state->n);
        fprintf(stderr, "  b: ..... %lu\n", state->b);
        fprintf(stderr, "  nlevels: [0, %lu]\n", state->nlevels - 1);
        fprintf(stderr, "  ηs: .... ");
    }
    state->etas = calloc(state->nlevels, sizeof state->etas[0]);
    for (size_t i = 0; i < state->nlevels; ++i) {
        if (i * 2 * b > eta) {
            fprintf(stderr, "error: η - ℓ·2B < 0\n");
            goto error;
        }
        state->etas[i] = eta - i * 2 * b;
        if (verbose)
            fprintf(stderr, " %lu", state->etas[i]);
    }
    if (verbose) {
        fprintf(stderr, "\n  Generating p_i's:\n");
    }
    state->ps = calloc(state->nlevels, sizeof state->ps[0]);
    for (size_t i = 0; i < state->nlevels; ++i) {
        state->ps[i] = calloc(state->n, sizeof state->ps[i][0]);
        if (verbose) {
            count = 0;
            print_progress(count, state->n);
        }
        for (size_t j = 0; j < state->n; ++j) {
            mpz_init(state->ps[i][j]);
            mpz_prime(state->ps[i][j], s->rngs[j], state->etas[i]);
            if (verbose)
                print_progress(++count, state->n);
        }
        if (verbose)
            fprintf(stderr, "\n");
    }
    if (verbose) {
        fprintf(stderr, "  Generating x0s:\n");
    }
    state->x0s = calloc(state->nlevels, sizeof state->x0s[0]);
    state->crt_coeffs = calloc(state->nlevels, sizeof state->crt_coeffs[0]);
    for (size_t i = 0; i < state->nlevels; ++i) {
        mpz_init(state->x0s[i]);
        if (verbose)
            fprintf(stderr, "  Computing product:\n");
        product(state->x0s[i], state->ps[i], state->n, verbose);
        state->crt_coeffs[i] = mpz_vector_new(state->n);
        crt_coeffs(state->crt_coeffs[i], state->ps[i], state->n, state->x0s[i], verbose);
    }
    state->switches = calloc(nswitches, sizeof state->switches[0]);
    for (size_t i = 0; i < nswitches; ++i) {
        if ((state->switches[i] = switch_state_new(s, 2, levels[i], verbose)) == NULL)
            goto error;
    }
    if (verbose)
        fprintf(stderr, "Polylog CLT initialization complete!\n");
    return state;
error:
    polylog_state_free(state);
    return NULL;
}

int
polylog_encode(clt_elem_t *rop, const clt_state_t *s, size_t n, mpz_t *xs, const int *ix, size_t level)
{
    (void) ix;                  /* XXX */
    polylog_state_t *pstate = s->pstate;
    mpz_t b_mpz;
    mpz_init_set_ui(b_mpz, pstate->b);
    mpz_set_ui(rop->elem, 0);
    for (size_t i = 0; i < n; ++i) {
        mpz_t tmp;
        mpz_init(tmp);
        /* rᵢ ∈ [-2ᴮ, 2ᴮ ] */
        mpz_urandomb_aes(tmp, s->rngs[i], 2 * pstate->b);
        mpz_mod_near(tmp, tmp, b_mpz);
        /* gᵢ · rᵢ */
        mpz_mul(tmp, tmp, s->gs[i]);
        /* mᵢ + gᵢ·rᵢ */
        mpz_add(tmp, tmp, xs[i]); /* XXX map to virtual slot */
        mpz_mul(tmp, tmp, pstate->crt_coeffs[level][i]);
        mpz_add(rop->elem, rop->elem, tmp);
        mpz_clear(tmp);
    }
    rop->level = level;
    /* XXX ignore index set for now */
    /* if (ix) { */
    /*     mpz_t tmp; */
    /*     mpz_init(tmp); */
    /*     /\* multiply by appropriate zinvs *\/ */
    /*     rop->ix = calloc(s->nzs, sizeof rop->ix[0]); */
    /*     for (unsigned long i = 0; i < s->nzs; ++i) { */
    /*         if (ix[i] <= 0) */
    /*             continue; */
    /*         rop->ix[i] = ix[i]; */
    /*         mpz_powm_ui(tmp, s->zinvs[i], ix[i], pstate->x0s[level]); */
    /*         mpz_mul_mod(rop->elem, rop->elem, tmp, pstate->x0s[level]); */
    /*     } */
    /*     mpz_clear(tmp); */
    /* } */
    return CLT_OK;
}

int
polylog_elem_mul(clt_elem_t *rop, const clt_state_t *s, const clt_elem_t *a, const clt_elem_t *b, size_t idx)
{
    switch_state_t *sstate;
    if (a->level != b->level) {
        fprintf(stderr, "error: levels unequal (%lu ≠ %lu), unable to multiply",
                a->level, b->level);
        return CLT_ERR;
    }
    sstate = s->pstate->switches[idx];
    /* a · b mod Π^(ℓ) */
    mpz_mul(rop->elem, a->elem, b->elem);
    mpz_mod(rop->elem, rop->elem, s->pstate->x0s[sstate->level]);
    rop->level = a->level;
    if (polylog_switch(rop, s, rop, sstate) == CLT_ERR)
        return CLT_ERR;
    return CLT_OK;
}

/* static void */
/* quotient(mpz_t rop, mpz_t a, mpz_t b) */
/* { */
/*     mpz_mod_near(rop, a, b); */
/*     mpz_sub(rop, a, rop); */
/*     mpz_div(rop, rop, b); */
/* } */

static mpz_t *
worddecomp(mpz_t x, size_t wordsize_, size_t k)
{
    mpz_t tmp, wordsize;
    mpz_t *decomp;

    mpz_init_set_ui(wordsize, wordsize_);
    mpz_init_set(tmp, x);
    decomp = calloc(k, sizeof decomp[0]);
    for (size_t i = 0; i < k; ++i) {
        mpz_init(decomp[i]);
        mpz_mod_near(decomp[i], tmp, wordsize);
        mpz_tdiv_q_2exp(tmp, tmp, (int) log2(wordsize_));
        /* quotient(tmp, tmp, wordsize); */
    }
    return decomp;
}

int
polylog_switch(clt_elem_t *rop, const clt_state_t *s, clt_elem_t *x_, switch_state_t *sstate)
{
    const polylog_state_t *pstate = s->pstate;
    mpz_t *pi, *pip, ct, wk;
    clt_elem_t *x;
    int ret = CLT_ERR;

    if (rop == NULL)
        return CLT_ERR;
    x = clt_elem_new();
    clt_elem_set(x, x_);
    if (sstate->level != x->level) {
        fprintf(stderr, "error: using switch parameter with a mismatched level\n");
        fprintf(stderr, "       element level: %lu\n", x->level);
        fprintf(stderr, "       switch level:  %lu\n", sstate->level);
        goto cleanup;
    }

    pi = &pstate->x0s[sstate->level];
    pip = &pstate->x0s[sstate->level + 1];

    mpz_inits(ct, wk, NULL);
    mpz_ui_pow_ui(wk, sstate->wordsize, sstate->k);
    mpz_set_ui(rop->elem, 0);
    for (size_t t = 0; t < pstate->theta; ++t) {
        mpz_t *cts;
        /* Compute cₜ = (x · yₜ) / Π^(ℓ) mod (wordsize)ᵏ */
        mpz_mul(ct, x->elem, sstate->ys[t]);
        mpz_tdiv_q(ct, ct, *pi);
        mpz_mod(ct, ct, wk);
        cts = worddecomp(ct, sstate->wordsize, sstate->k);
        for (size_t i = 0; i < sstate->k; ++i) {
            mpz_t tmp;
            mpz_init(tmp);
            mpz_mul(tmp, cts[i], sstate->sigmas[t][i]->elem);
            mpz_add(rop->elem, rop->elem, tmp);
            mpz_mod(rop->elem, rop->elem, *pip);
            mpz_clear(tmp);
        }
    }
    rop->level = x->level + 1;
    ret = CLT_OK;
cleanup:
    clt_elem_free(x);
    return ret;
}

int
polylog_elem_decrypt(clt_elem_t *rop, const clt_state_t *s, size_t level)
{
    printf("DECRYPTION @ LEVEL %lu :: ", level);
    for (size_t i = 0; i < s->n; ++i) {
        mpz_mod_near(rop->elem, rop->elem, s->pstate->ps[level][i]);
        mpz_mod_near(rop->elem, rop->elem, s->gs[i]);
        gmp_printf("%Zd ", rop->elem);
    }
    printf("\n");
    return CLT_OK;
}
