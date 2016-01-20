#ifndef GOP_CALLSTACK_H_INCLUDED
#define GOP_CALLSTACK_H_INCLUDED

#include "refcount.h"

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

// Info about the frame
// First two bits track if the stack is running
#define GOP_CS_FLAG_PENDING 0x00
#define GOP_CS_FLAG_RUNNING 0x01
#define GOP_CS_FLAG_FINISHED 0x02

// Initialize frame in caller context
extern void cs_frame_generic_init(cs_frame_t ** frame, cs_depth_t * depth);
// Just before executing op within callee context
extern void cs_frame_generic_begin(int mode);
// Just after executing op within callee context
extern void cs_frame_generic_end();

#define cs_frame_tp_direct_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_direct_begin(th, wrap) cs_frame_generic_begin(GOP_CS_MODE_TLS)
#define cs_frame_tp_direct_end(th) cs_frame_generic_end()
#define cs_frame_tp_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_begin(op) cs_frame_generic_begin(GOP_CS_MODE_TLS)
#define cs_frame_tp_end(status) cs_frame_generic_end()

// When ops are executed synchronously
#define cs_frame_sync_begin() cs_frame_generic_begin(GOP_CS_MODE_SYNC)
#define cs_frame_sync_end() cs_frame_generic_end()

#endif
