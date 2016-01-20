#ifndef GOP_CALLSTACK_H_INCLUDED
#define GOP_CALLSTACK_H_INCLUDED

#include "refcount.h"
#include <apr_thread_proc.h>

typedef struct cs_frame_s cs_frame_t;
typedef uint32_t cs_depth_t;
typedef struct cs_frame_s {
    cs_frame_t * mother;
    cs_frame_t * tls_old;
    void * data;
    cs_depth_t depth;
    tb_ref_t refcount;
    volatile uint32_t flags;
} cs_frame_t;

// Where/how to transfer previous frame
#define GOP_CS_MODE_TLS 1
#define GOP_CS_MODE_SYNC 2
#define GOP_CS_MODE_TP_OWNER 3

// Info about the frame
// First two bits track if the stack is running
#define GOP_CS_FLAG_PENDING 0
#define GOP_CS_FLAG_RUNNING 1
#define GOP_CS_FLAG_FINISHED 2

// Start up callstack system
extern int cs_init();
// Initialize frame in caller context
extern void cs_frame_generic_init(cs_frame_t ** frame, cs_depth_t * depth);
// Just before executing op within callee context
extern void cs_frame_generic_begin(int mode, apr_thread_t * thread, void * data);
// Just after executing op within callee context
extern void cs_frame_generic_end();
// Check the currently executing depth
extern cs_depth_t cs_current_depth_get();

#define cs_frame_tp_direct_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_direct_begin(th, wrap) cs_frame_generic_begin(GOP_CS_MODE_TP_OWNER, (th), (NULL))
#define cs_frame_tp_direct_end(th) cs_frame_generic_end()
#define cs_frame_tp_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_begin(th, op) cs_frame_generic_begin(GOP_CS_MODE_TP_OWNER, (th), (op))
#define cs_frame_tp_end(status) cs_frame_generic_end()

// When ops are executed synchronously
#define cs_frame_sync_begin() cs_frame_generic_begin(GOP_CS_MODE_SYNC, NULL, NULL)
#define cs_frame_sync_end() cs_frame_generic_end()

#endif
