#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// NOTE: Book-style QSBR (Fig. 19.8) reclaims at every op_end(); no batching.
// Kept for later performance experiments.
#define RETIRE_THRESHOLD 64

typedef int value_t;

typedef struct node {
    value_t data;
    _Atomic(struct node*) next;
} node_t;

typedef struct freelist {
    node_t* head;
} freelist_t;

typedef struct retired {
    node_t* node;
    struct retired* next;
} retired_t;

typedef struct queue {
    _Atomic(node_t*) head;
    _Atomic(node_t*) tail;
    freelist_t* free_lists;
    int num_threads;

    // --- QSBR/EBR-style reclamation ---
    _Atomic uint64_t* ctr;       // ctr[tid]: odd=in op, even=quiescent
    retired_t** retired;         // retired[tid]: private list head
    size_t* retired_count;       // retired_count[tid]
} queue_t;


node_t* make_node(value_t v);
node_t* get_next(node_t* n);
void init_queue(queue_t* Q, int num_threads);
void destroy_queue(queue_t* Q);
node_t* upcylce_node(queue_t* q, int tid);
void recycle_node(queue_t* Q, int tid, node_t* node);
int enq(value_t v, queue_t* Q, int thread_id);
int deq(value_t *v, queue_t* Q, int thread_id);

// Test helper: pauses just before attempting the head CAS in the dequeue fast-path.
// Used to force ABA-style interleavings in unit tests.
// `ready` is set to 1 after capturing (first,next); the caller sets `go` to 1 to resume.
int deq_pause_before_cas(value_t* v, queue_t* Q, int thread_id,
                         _Atomic int* ready, _Atomic int* go,
                         node_t** observed_first, node_t** observed_next);