// Catherine Bley - 12002266
// Moritz Jasper Techen - 12432927
// Tobias Ponesch - 11818774
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// NOTE: Book-style QSBR (Fig. 19.8) reclaims at every op_end(); no batching.
typedef int value_t;

typedef struct node {
    value_t data;
    _Atomic(struct node*) next;
} node_t;

typedef struct freelist {
    node_t* head;
    int size;
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

    _Atomic uint64_t* ctr;
    retired_t** retired;
    size_t* retired_count;
} queue_t;


node_t* make_node(value_t v);
node_t* get_next(node_t* n);
void init_queue(queue_t* Q, int num_threads);
void destroy_queue(queue_t* Q);
node_t* upcylce_node(queue_t* Q, int tid);
void recycle_node(queue_t* Q, int tid, node_t* node, int* free_list_insertion_count);
int enq(value_t v, queue_t* Q, int thread_id, int* failed_CAS_count, int* free_list_insertion_count);
int deq(value_t *v, queue_t* Q, int thread_id, int* failed_CAS_count, int* free_list_insertion_count);