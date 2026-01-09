#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <omp.h>

typedef int value_t;

typedef struct node {
    value_t data;
    struct node* next;
} node_t;

typedef struct freelist {
    node_t* head;
    int size;
} freelist_t;

typedef struct peterson_lock {
    volatile bool flag[2];
    volatile uint8_t victim;
} peterson_lock_t;

typedef struct queue {
    node_t* head;
    node_t* tail;
    freelist_t* free_lists;
    int num_threads;
    omp_lock_t enq_lock;
    omp_lock_t deq_lock;
    peterson_lock_t plock;
} queue_t;

node_t* make_node(value_t v);
void init_queue(queue_t* Q, int num_threads);
void destroy_queue(queue_t* Q);
void lock_peterson(peterson_lock_t* plock, uint8_t flag_id);
void unlock_peterson(peterson_lock_t* plock, uint8_t flag_id);
node_t* upcylce_node(queue_t* Q, int tid);
void recycle_node(queue_t* Q, int tid, node_t* node);
int enq(value_t v, queue_t* Q, int thread_id);
int deq(value_t *v, queue_t* Q, int thread_id);