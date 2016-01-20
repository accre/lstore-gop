#ifndef GOP_CONTROL_H_INCLUDED
#define GOP_CONTROL_H_INCLUDED

// Stores functions for gop_control_t structure

#include <apr_thread_cond.h>
#include <apr_thread_mutex.h>
#include "pigeon_coop.h"

typedef struct gop_control_s {
    apr_thread_mutex_t *lock;  //** shared lock
    apr_thread_cond_t *cond;   //** shared condition variable
    pigeon_coop_hole_t  pch;   //** Pigeon coop hole for the lock and cond
} gop_control_t;

extern void *gop_control_new(void *arg, int size);
extern void gop_control_free(void *arg, int size, void *data);

#define gop_control_cond_broadcast(op) apr_thread_cond_broadcast((op)->base.ctl->cond)
#define gop_control_cond_timedwait(op, adt) \
        apr_thread_cond_timedwait((op)->base.ctl->cond, (op)->base.ctl->lock, adt)
#define gop_control_cond_wait(op) \
        apr_thread_cond_wait((op)->base.ctl->cond, (op)->base.ctl->lock)

#define gop_control_q_cond_broadcast(q) apr_thread_cond_broadcast((q)->opque->op.base.ctl->cond)
#define gop_control_q_cond_timedwait(q, adt) \
        apr_thread_cond_timedwait((q)->opque->op.base.ctl->cond, (q)->opque->op.base.ctl->lock, adt)
#define gop_control_q_cond_wait(q, op) \
        apr_thread_cond_timedwait((q)->opque->op.base.ctl->cond, (q)->opque->op.base.ctl->lock)

#endif
