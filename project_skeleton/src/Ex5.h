#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

// stamped pointer implementation similiar to Java's AtomicStampedReference
typedef _Atomic(uint64_t) atomic_stamped_ptr_t;
#define PTR_MASK 0x0000FFFFFFFFFFFFULL
#define STAMP_SHIFT 48

typedef int value_t;

typedef struct node {
    value_t data;
    atomic_stamped_ptr_t next;
} node_t;

typedef struct freelist {
    node_t* head;
} freelist_t;

typedef struct queue {
    atomic_stamped_ptr_t head;
    atomic_stamped_ptr_t tail;
    freelist_t* free_lists;
    int num_threads;
} queue_t;

static inline uint64_t pack(node_t *ptr, uint16_t stamp) {
    return ((uint64_t)stamp << STAMP_SHIFT) | ((uint64_t)ptr & PTR_MASK);
}

static inline node_t* unpack_ptr(uint64_t packed) {
    return (node_t*)(packed & PTR_MASK);
}

static inline uint16_t unpack_stamp(uint64_t packed) {
    return (uint16_t)(packed >> STAMP_SHIFT);
}

static inline bool compare_and_set_stamped(
    atomic_stamped_ptr_t* addr,
    node_t* expected_ptr,
    node_t* new_ptr,
    uint16_t expected_stamp,
    uint16_t new_stamp)
{
    uint64_t expected = pack(expected_ptr, expected_stamp);
    uint64_t desired = pack(new_ptr, new_stamp);
    return atomic_compare_exchange_strong(addr, &expected, desired);
}


node_t* make_node(value_t v);
node_t* get_next(node_t* n);
void init_queue(queue_t* Q, int num_threads);
void destroy_queue(queue_t* Q);
node_t* upcylce_node(queue_t* q, int tid);
void recycle_node(queue_t* Q, int tid, node_t* node);
int enq(value_t v, queue_t* Q, int thread_id);
uint16_t getStamp(atomic_stamped_ptr_t* addr);
node_t* getReference(atomic_stamped_ptr_t* addr);
node_t* get(atomic_stamped_ptr_t* addr, uint16_t* stampHolder);
int deq(value_t *v, queue_t* Q, int thread_id);