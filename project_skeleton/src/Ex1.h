#include <stdlib.h>

typedef int value_t;

typedef struct node {
    value_t data;
    struct node* next;
} node_t;

typedef struct freelist {
    node_t* head;
} freelist_t;

typedef struct queue {
    node_t* head;
    node_t* tail;
    freelist_t* free_lists;
    int num_threads;
} queue_t;

node_t* make_node(value_t v);
void init_queue(queue_t* Q, int num_threads);
void destroy_queue(queue_t* Q);
node_t* upcylce_node(queue_t* q, int tid);
int enq(value_t v, queue_t* Q, int thread_id);
int deq(value_t* v, queue_t* Q, int thread_id);