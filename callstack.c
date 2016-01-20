// A call-stack to track the parent/child dependencies between operations. From
// the perspective of the callee, this appears to be a call stack, but the sum
// of all the stacks forms a call graph

// On submission of an op, first produce a new frame. Then, attempt to divine
// out if we are currently executing within an op, and set up a new frame
// linking the to-be-executed op with its mother. If we cant, point to a global
// root frame.

// The hitch is that we don't want to have to force every callsite to change to
// keep a hold of the current frame and pass it into GOP. To sidestep this, we
// stash a pointer to the currently-executing frame in the following locations:
//
// 1) The owner parameter of apr_thread_pool.
// 2) Thread-local storage
//

#include <apr_atomic.h>
#include <apr_portable.h>
#include <apr_thread_proc.h>
#include <apr_thread_pool.h>
#include "callstack.h"
#include "refcount.h"
#include "log.h"
#include <stdlib.h>

// Globals
static cs_frame_t cs_root = {
    .mother = NULL,
    .depth = 0,
    .refcount = {NULL, 1}
};
static apr_threadkey_t * tls_threadkey = NULL;
static apr_pool_t * pool = NULL;
static volatile apr_uint32_t frame_count = 1;

// Forward declaration
static cs_frame_t * cs_current_frame_get();
static cs_frame_t * cs_current_tls_frame_get();
static void cs_current_tls_frame_set(cs_frame_t * frame);
static void cs_frame_apr_cleanup_destroy(void * frame);
static cs_frame_t * cs_child_new(cs_frame_t * mother);
static cs_frame_t * cs_new();
static void cs_refcount_free(const tb_ref_t * ref);
static void cs_frame_set_state(cs_frame_t * frame, uint32_t state);
static cs_frame_t * cs_frame_mother_search(int mode, apr_thread_t * th);

// External interface

// Initialize callstack system
int cs_init() {
    if (apr_pool_create(&pool, NULL), APR_SUCCESS) {
        return 1;
    }
    if (apr_threadkey_private_create(&tls_threadkey,
                                     cs_frame_apr_cleanup_destroy,
                                     pool) != APR_SUCCESS) {
        return 1;
    }
    return 0;
}

// Called from the caller environment. Caller responsible for sending frame
// to callee. Frame's reference count is initialized to 1, mother gains a ref
// from her child
void cs_frame_generic_init(cs_frame_t ** frame, cs_depth_t * depth) {
    cs_frame_t * mother = cs_current_frame_get();
    *frame = cs_child_new(mother);
    if (depth)
        *depth = (*frame)->depth;
    cs_frame_set_state(*frame, GOP_CS_FLAG_PENDING);
    if (mother)
        tb_ref_dec(&mother->refcount);
    assert(tb_ref_get(&(*frame)->refcount) == 1);
}

// Called from the callee environment
void cs_frame_generic_begin(int mode, apr_thread_t * thread, void * data) {
    cs_frame_t * current = cs_frame_mother_search(mode, thread);
    cs_frame_set_state(current, GOP_CS_FLAG_RUNNING);
    cs_frame_t * tls_old = cs_current_tls_frame_get();
    current->tls_old = tls_old;
    cs_current_tls_frame_set(current);
    tb_ref_dec(&current->refcount);
}

// Called from the callee environment
void cs_frame_generic_end() {
    cs_frame_t * current = cs_current_frame_get();
    cs_frame_set_state(current, GOP_CS_FLAG_FINISHED);
    cs_current_tls_frame_set(current->tls_old);
    if (current->tls_old) {
        tb_ref_dec(&current->tls_old->refcount);
        current->tls_old = NULL;
    }
    tb_ref_dec(&current->refcount);
}

// Verify the currently executing depth
cs_depth_t cs_current_depth_get() {
    cs_frame_t * current = cs_current_frame_get();
    cs_depth_t ret = current->depth;
    tb_ref_dec(&current->refcount);
    return ret;
}

// Private interface

// Given a mode, find the current slash previous frame
static
cs_frame_t * cs_frame_mother_search(int mode, apr_thread_t * th) {
    cs_frame_t * current = NULL;
    if (mode == GOP_CS_MODE_TP_OWNER) {
        if (apr_thread_pool_task_owner_get(th, (void **) &current) 
                                                            != APR_SUCCESS) {
            current = NULL;
        }
    } else if (mode == GOP_CS_MODE_SYNC) {
        // Guaranteed to have refcount of 1
        cs_frame_generic_init(&current, NULL);
    } else if (mode == GOP_CS_MODE_TLS) {
        // Increments current refcount
        current = cs_current_tls_frame_get();
    }
    if (!current) {
        current = &cs_root;
        tb_ref_inc(&current->refcount);
    }
    return current;
}

// Updates the running state of a frame. Currently unused, but the hook is
// useful for later commits.
static
void cs_frame_set_state(cs_frame_t * frame, uint32_t state) {

}

// Retrieves current frame if possible, root frame otherwise
// Return cs_root on error
// The return value gains one additional reference
static
cs_frame_t * cs_current_frame_get() {
    cs_frame_t * frame = cs_current_tls_frame_get();
    cs_frame_t * retval;
    
    if (frame) {
        // cs_current_tls_frame_get already paid for the reference
        retval = frame;
    } else {
        // manually bump the root reference count
        retval = &cs_root;
        tb_ref_inc(&retval->refcount);
    }
    // Increment the refcount
    return retval;
}

// Retrieves current TLS frame
// Return NULL on error
// The return value gains one additional reference
static
cs_frame_t * cs_current_tls_frame_get() {
    cs_frame_t * ret;
    apr_threadkey_private_get((void **) & ret, tls_threadkey);
    // There's the pointer in TLS, but now I've made a new copy of it
    if (ret)
        tb_ref_inc(&ret->refcount);

    return ret;
}

// Sets current TLS frame
// On success, decrements old refcount and increments new refcount
void cs_current_tls_frame_set(cs_frame_t * frame) {
    cs_frame_t * prev = cs_current_tls_frame_get();
    apr_threadkey_private_set((void *) frame, tls_threadkey);
    if (frame)
        tb_ref_inc(&frame->refcount);
    // One reference for the old TLS entry
    // One reference for the local temporary
    if (prev) {
        tb_ref_dec(&prev->refcount);
        tb_ref_dec(&prev->refcount);
    }
}

// Increments mother refcount
// Child refcount initialized to 1.
// Return NULL on error
static
cs_frame_t * cs_child_new(cs_frame_t * mother) {
	cs_frame_t * child = cs_new();
    if (!child)
        return child;
    tb_ref_inc(&child->refcount);
    tb_ref_inc(&mother->refcount);
    child->mother = mother;
    child->depth = mother->depth + 1;
    assert(tb_ref_get(&child->refcount) == 1);
    return child;
}

// Allocate a new bare frame with zero references
// Returns NULL on error
static
cs_frame_t * cs_new() {
    cs_frame_t * frame = (cs_frame_t *) malloc(sizeof(cs_frame_t));
    if (!frame)
        return frame;
    memset((void *)frame, 0, sizeof(cs_frame_t));
    frame->refcount.free = cs_refcount_free;
    apr_atomic_inc32(&frame_count);
    return frame;
}

// Called via APR to destroy a frame object
static
void cs_frame_apr_cleanup_destroy(void * frame) {
    tb_ref_dec(&((cs_frame_t *) frame)->refcount);
}

// Called when reference count hits zero
static
void cs_refcount_free(const tb_ref_t * ref) {
    cs_frame_t * frame = container_of(ref, cs_frame_t, refcount);
    if (frame->mother)
        tb_ref_dec(&frame->mother->refcount);
    apr_atomic_dec32(&frame_count);
    log_printf(10, "Frame cleared, %i remaining\n", apr_atomic_read32(&frame_count));
    free((void *) frame);
}
