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

#include <apr_portable.h>
#include <apr_thread_proc.h>
#include <apr_thread_pool.h>
#include "callstack.h"
#include "refcount.h"
#include <stdlib.h>

// Globals
cs_frame_t cs_root = {
    .mother = NULL,
    .depth = 0,
    .refcount = {NULL, 1}
};
const char * tls_key_name = "GOP_CS_CURRENT_FRAME";


// Forward declaration
static cs_frame_t * cs_current_frame_get();
static cs_frame_t * cs_current_tls_frame_get();
static void cs_current_tls_frame_set(cs_frame_t * frame);
static cs_frame_t * cs_current_tls_frame_get_thread(apr_thread_t * thread);
static cs_frame_t * cs_current_tp_frame_get_thread(apr_thread_t * thread);
static apr_thread_t * cs_current_thread_get();
static apr_status_t cs_frame_apr_cleanup_destroy(void * frame);
static cs_frame_t * cs_child_new(cs_frame_t * mother);
static cs_frame_t * cs_new();
static void cs_refcount_free(const tb_ref_t * ref);
static void cs_frame_set_state(cs_frame_t * frame, uint32_t state);

// External interface

// Called from the caller environment. Caller responsible for sending frame
// to callee. Frame's reference count is initialized to 1
void cs_frame_generic_init(cs_frame_t ** frame, cs_depth_t * depth) {
    // Following increments refcount of mother
    cs_frame_t * mother = cs_current_frame_get();
    *frame = cs_child_new(mother);
    *depth = (*frame)->depth;
    cs_frame_set_state(*frame, GOP_CS_FLAG_PENDING);
    if (mother)
        tb_ref_dec(&mother->refcount);
}

// Called from the callee environment
void cs_frame_generic_begin(int mode) {
    cs_frame_t * current;
    if (mode == GOP_CS_MODE_SYNC) {
        // Guaranteed to have refcount of 1
        cs_frame_generic_init(&current, NULL);
    } else {
        // Increments current refcount
        current = cs_current_frame_get();
    }
    if (current && (current != &cs_root)) {
        cs_frame_set_state(current, GOP_CS_FLAG_RUNNING);
        // Ownershipt xfers from old tls to current->tls_old.
        // Increment refcount of current->tls_old....
        cs_frame_t * tls_old = cs_current_tls_frame_get();
        current->tls_old = tls_old;

        // ... then destructor decrements previous objects ref
        cs_current_tls_frame_set(current);
    }
    if (current && (mode != GOP_CS_MODE_SYNC))
        tb_ref_dec(&current->refcount);
}

// Called from the callee environment
void cs_frame_generic_end() {
    // Following increments refcount of current
    cs_frame_t * current = cs_current_frame_get();
    if (current && (current != &cs_root)) {
        cs_frame_set_state(current, GOP_CS_FLAG_FINISHED);
        // Increments refcount of current->tls_old
        // Destructor of previous TLS called by APR.
        cs_current_tls_frame_set(current->tls_old);
        current->tls_old = NULL;
    }
    if (current)
        tb_ref_dec(&current->refcount);
}

// Private interface

// Updates the running state of a frame. Currently unused, but the hook is
// useful for later commits.
static
void cs_frame_set_state(cs_frame_t * frame, uint32_t state) {

}

// Retrieves current frame if possible, root frame otherwise
// Increments refcount
// Return cs_root on error
static
cs_frame_t * cs_current_frame_get() {
    apr_thread_t * thread;
    cs_frame_t * frame1;
    cs_frame_t * frame2;
    cs_frame_t * retval;
    
    // In case we don't find anything, return the root object
    retval = &cs_root;

    if ((thread = cs_current_thread_get()) != NULL) {
        // Search thread pool
        frame1 = cs_current_tp_frame_get_thread(thread);
        
        // Search Thread Local Storage
        frame2 = cs_current_tls_frame_get_thread(thread);

        // If we got two frames, return the one with the deepest depth
        if (frame1 && frame2) {
            retval = (frame1->depth >= frame2->depth) ? frame1 : frame2;
        } else if (frame1 || frame2) {
            retval = (frame1) ? frame1 : frame2;
        }
    }

    // Increment the refcount
    tb_ref_inc(&retval->refcount);
    return retval;
}

// Retrieves current TLS frame
// Increments refcount
// Return NULL on error
static
cs_frame_t * cs_current_tls_frame_get() {
    apr_thread_t * th = cs_current_thread_get();
    if (th) {
        cs_frame_t * ret = cs_current_tls_frame_get_thread(th);
        tb_ref_inc(&ret->refcount);
        return ret;
    } else {
        return NULL;
    }
}

// Sets current TLS frame
// Increments refcount
void cs_current_tls_frame_set(cs_frame_t * frame) {
    apr_thread_t * th = cs_current_thread_get();
    if (!th)
        return;
    apr_thread_data_set((void *) frame,
                                    tls_key_name,
                                    cs_frame_apr_cleanup_destroy,
                                    th);
    tb_ref_inc(&frame->refcount);
}

// Retrieve TLS copy of current frame
// Does not increment refcount
// Return NULL on error
static
cs_frame_t * cs_current_tls_frame_get_thread(apr_thread_t * thread) {
    cs_frame_t * ret = NULL;
    if (APR_SUCCESS != apr_thread_data_get((void **)& ret,
                                     tls_key_name,
                                     thread)) {
        ret = NULL;
    }
    return ret;
}

// Retrieve threadpool's copy of the frame
// Does not increment refcount
// Return NULL on error
static
cs_frame_t * cs_current_tp_frame_get_thread(apr_thread_t * thread) {
    cs_frame_t * ret = NULL;
    if (APR_SUCCESS != apr_thread_pool_task_owner_get(thread,
                                                     (void **) &ret)) {
        ret = NULL;
    }
    return ret;

}

// Retrieves the apr_thread_t of the current thread. Why is this not already a
// helper function?
static
apr_thread_t * cs_current_thread_get() {
    apr_os_thread_t os_thread;
    apr_thread_t * thread;
    os_thread = apr_os_thread_current();
    if (APR_SUCCESS == apr_os_thread_put(&thread, &os_thread, NULL)) {
        return thread;
    } else {
        return NULL;
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
    tb_ref_inc(&mother->refcount);
    child->mother = mother;
    child->depth = mother->depth + 1;
    return child;
}

// Allocate a new bare frame
// Returns NULL on error
static
cs_frame_t * cs_new() {
    cs_frame_t * frame = (cs_frame_t *) malloc(sizeof(cs_frame_t));
    if (!frame)
        return frame;
    memset((void *)frame, 0, sizeof(cs_frame_t));
    frame->refcount.free = cs_refcount_free;
    return frame;
}

// Called via APR to destroy a frame object
static
apr_status_t cs_frame_apr_cleanup_destroy(void * frame) {
    tb_ref_dec(&((cs_frame_t *) frame)->refcount);
    return APR_SUCCESS;
}

// Called when reference count hits zero
static
void cs_refcount_free(const tb_ref_t * ref) {
    cs_frame_t * frame = container_of(ref, cs_frame_t, refcount);
    tb_ref_dec(&frame->mother->refcount);
    free((void *) frame);
}
