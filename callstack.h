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
} cs_frame_t;

// Initialize frame in caller context
extern void cs_frame_generic_init(cs_frame_t ** frame, cs_depth_t * depth);
// Just before executing op within callee context
extern void cs_frame_generic_begin();
// Just after executing op within callee context
extern void cs_frame_generic_end();

#define cs_frame_tp_direct_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_direct_begin(th, wrap) cs_frame_generic_begin()
#define cs_frame_tp_direct_end(th) cs_frame_generic_end()
#define cs_frame_tp_init(frame, depth) cs_frame_generic_init(frame, depth)
#define cs_frame_tp_begin(op) cs_frame_generic_begin()
#define cs_frame_tp_end(status) cs_frame_generic_end()

#endif
