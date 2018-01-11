#include "clt13.h"
#include "clt_elem.h"
#include "utils.h"

#include <omp.h>

clt_elem_t *
clt_elem_new(void)
{
    clt_elem_t *e = calloc(1, sizeof e[0]);
    mpz_init(e->elem);
    return e;
}

void
clt_elem_free(clt_elem_t *e)
{
    mpz_clear(e->elem);
    free(e);
}

void
clt_elem_set(clt_elem_t *a, const clt_elem_t *b)
{
    mpz_set(a->elem, b->elem);
}

void
clt_elem_print(const clt_elem_t *a)
{
    gmp_printf("%Zd", a->elem);
}

int
clt_elem_fread(clt_elem_t *x, FILE *fp)
{
    if (mpz_fread(x->elem, fp) == CLT_ERR)
        return CLT_ERR;
    return CLT_OK;
}

int
clt_elem_fwrite(clt_elem_t *x, FILE *fp)
{
    if (mpz_fwrite(x->elem, fp) == CLT_ERR)
        return CLT_ERR;
    return CLT_OK;
}
