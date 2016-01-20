#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>
#include "assert_result.h"
#include "gop_control.h"
#include "type_malloc.h"

extern apr_pool_t *_opque_pool;
extern pigeon_coop_t *_gop_control;

//*************************************************************
// gop_control_new - Creates a new gop_control shelf set
//*************************************************************

void *gop_control_new(void *arg, int size)
{
    gop_control_t *shelf;
    int i;

    type_malloc_clear(shelf, gop_control_t, size);

    for (i=0; i<size; i++) {
        assert_result(apr_thread_mutex_create(&(shelf[i].lock), APR_THREAD_MUTEX_DEFAULT,_opque_pool), APR_SUCCESS);
        assert_result(apr_thread_cond_create(&(shelf[i].cond), _opque_pool), APR_SUCCESS);
    }

    return((void *)shelf);
}

//*************************************************************
// gop_control_free - Destroys a gop_control set
//*************************************************************

void gop_control_free(void *arg, int size, void *data)
{
    gop_control_t *shelf = (gop_control_t *)data;
    int i;

    for (i=0; i<size; i++) {
        apr_thread_mutex_destroy(shelf[i].lock);
        apr_thread_cond_destroy(shelf[i].cond);
    }

    free(shelf);
    return;
}


