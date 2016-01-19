// Generic reference counting
// Adapted from: http://nullprogram.com/blog/2015/02/17/
#ifndef GOP_REFCOUNT_H_INCLUDED
#define GOP_REFCOUNT_H_INCLUDED
#include <apr_atomic.h>
#include <assert.h>
#include <stddef.h>

// Given a pointer to a ref embedded in another struct, return the address of
// the outer struct.
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))


typedef struct tb_ref_s tb_ref_t;
struct tb_ref_s {
    void (*free)(const tb_ref_t *);
    volatile apr_uint32_t count;
};

static inline void tb_ref_inc(const tb_ref_t * ref) {
    apr_atomic_inc32((apr_uint32_t *) &ref->count);
}

static inline void tb_ref_dec(const tb_ref_t * ref) {
    assert(apr_atomic_read32((apr_uint32_t *) &ref->count) != 0);
    if (apr_atomic_dec32((apr_uint32_t *) &ref->count) == 0) {
        if (ref->free) {
            ref->free(ref);
        }
    }
}

#endif
/* Example usage:
 struct node {
     char id[64];
     float value;
     struct node *next;
     struct ref refcount;
 };

 static void
 node_free(const struct ref *ref)
 {
     struct node *node = container_of(ref, struct node, refcount);
     struct node *child = node->next;
     free(node);
     if (child)
         ref_dec(&child->refcount);
 }
 
 struct node *
 node_create(char *id, float value)
 {
     struct node *node = malloc(sizeof(*node));
     snprintf(node->id, sizeof(node->id), "%s", id);
     node->value = value;
     node->next = NULL;
     node->refcount = (struct ref){node_free, 1};
     return node;
 }
 
 void
 node_push(struct node **nodes, char *id, float value)
 {
     struct node *node = node_create(id, value);
     node->next = *nodes;
     *nodes = node;
 }
 
 struct node *
 node_pop(struct node **nodes)
 {
     struct node *node = *nodes;
     *nodes = (*nodes)->next;
     if (*nodes)
         ref_inc(&(*nodes)->refcount);
     return node;
 }
 
 void
 node_print(struct node *node)
 {
     for (; node; node = node->next)
         printf("%s = %f\n", node->id, node->value);
 }
 
 int main(void)
 {
     struct node *nodes = NULL;
     char id[64];
     float value;
     while (scanf(" %63s %f", id, &value) == 2)
         node_push(&nodes, id, value);
     if (nodes != NULL) {
         node_print(nodes);
         struct node *old = node_pop(&nodes);
         node_push(&nodes, "foobar", 0.0f);
         node_print(nodes);
         ref_dec(&old->refcount);
         ref_dec(&nodes->refcount);
     }
     return 0;
 }
 */
