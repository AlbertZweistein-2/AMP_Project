#include <Ex5.h>

static inline int is_odd(uint64_t x) {
    return (int)(x & 1ULL);
}

static inline void mm_op_begin(queue_t* Q, int tid) {
    // even -> odd (thread is inside a queue operation)
    atomic_fetch_add_explicit(&Q->ctr[tid], 1, memory_order_seq_cst);
}

static void mm_wait_until_unreserved(queue_t* Q, int tid) {
    for (int t = 0; t < Q->num_threads; t++) {
        if (t == tid) continue;

        uint64_t v = atomic_load_explicit(&Q->ctr[t], memory_order_seq_cst);

        // Even => thread is quiescent already
        if (!is_odd(v)) continue;

        // Odd => wait until the counter changes
        while (atomic_load_explicit(&Q->ctr[t], memory_order_seq_cst) == v) {
            // busy-wait
        }
    }
}

static void mm_reclaim_all(queue_t* Q, int tid) {
    retired_t* r = Q->retired[tid];
    Q->retired[tid] = NULL;
    Q->retired_count[tid] = 0;

    while (r) {
        retired_t* nxt = r->next;
        recycle_node(Q, tid, r->node);
        free(r);
        r = nxt;
    }
}

static inline void mm_op_end(queue_t* Q, int tid) {
    // odd -> even (thread is outside a queue operation)
    atomic_fetch_add_explicit(&Q->ctr[tid], 1, memory_order_seq_cst);

    // Book-style (The Art of Multiprocessor Programming, Fig. 19.8):
    // reclaim all pending retired nodes at op_end().
    if (Q->retired_count[tid] == 0) {
        return;
    }
    mm_wait_until_unreserved(Q, tid);
    mm_reclaim_all(Q, tid);
}

static void mm_retire(queue_t* Q, int tid, node_t* n) {
    retired_t* r = malloc(sizeof(*r));
    r->node = n;
    r->next = Q->retired[tid];
    Q->retired[tid] = r;
    Q->retired_count[tid]++;
}

node_t* make_node(value_t v)
{
    node_t* new_node = malloc(sizeof(node_t));
    new_node->data = v;
    atomic_init(&new_node->next, NULL);
    return new_node;
}

node_t* get_next(node_t* n)
{
    return (node_t*)atomic_load(&n->next);
}


void init_queue(queue_t* Q, int num_threads)
{
    node_t* dummy = make_node(0);
    atomic_init(&Q->head, dummy);
    atomic_init(&Q->tail, dummy);

    Q->free_lists = calloc(num_threads, sizeof(freelist_t));
    Q->num_threads = num_threads;

    Q->ctr = calloc(num_threads, sizeof(_Atomic uint64_t));
    Q->retired = calloc(num_threads, sizeof(retired_t*));
    Q->retired_count = calloc(num_threads, sizeof(size_t));

    for (int i = 0; i < num_threads; i++) {
        atomic_init(&Q->ctr[i], 0);
        Q->retired[i] = NULL;
        Q->retired_count[i] = 0;
    }
    return;
}

void destroy_queue(queue_t* Q)
{
    node_t* next;
    int nthreads = Q->num_threads;

    /* free main queue nodes */
    
    for (node_t* head_ptr = (node_t*)atomic_load(&Q->head); head_ptr != NULL;) {
        node_t* next = get_next(head_ptr);
        free(head_ptr);
        head_ptr = next;
    }

    /* free nodes stored in each thread-local free list */
    if (Q->free_lists) {
        for (int i = 0; i < nthreads; ++i) {
            node_t* n = Q->free_lists[i].head;
            while (n != NULL) {
                node_t* tmp = get_next(n);
                free(n);
                n = tmp;
            }
        }
        free(Q->free_lists);
        Q->free_lists = NULL;
    }

    /* free retired nodes (not yet reclaimed into any freelist) */
    if (Q->retired) {
        for (int i = 0; i < nthreads; i++) {
            retired_t* r = Q->retired[i];
            while (r) {
                retired_t* nxt = r->next;
                free(r->node);
                free(r);
                r = nxt;
            }
        }
        free(Q->retired);
        Q->retired = NULL;
    }

    if (Q->ctr) {
        free(Q->ctr);
        Q->ctr = NULL;
    }

    if (Q->retired_count) {
        free(Q->retired_count);
        Q->retired_count = NULL;
    }

    Q->num_threads = 0;
    return;
}

// pull from local free list -> use in enqueue
node_t* upcylce_node(queue_t* Q, int tid) 
{
    node_t* n = Q->free_lists[tid].head;
    if (n != NULL) {
        Q->free_lists[tid].head = get_next(n);
        return n;
    }
    n = malloc(sizeof(node_t));
    atomic_init(&n->next, NULL);
    return n;
}

// push in local free list -> use in dequeue
void recycle_node(queue_t* Q, int tid, node_t* node) 
{
    atomic_store(&node->next, Q->free_lists[tid].head);
    Q->free_lists[tid].head = node;
    return;
}

int enq(value_t v, queue_t* Q, int thread_id)
{
    mm_op_begin(Q, thread_id);
    node_t* n = upcylce_node(Q, thread_id);
    n->data = v;
    atomic_store(&n->next, NULL);

    while (true)
    {
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&last->next);

        if (last == atomic_load(&Q->tail)) {
            if (next == NULL) {
                node_t* expected_next = NULL;
                if (atomic_compare_exchange_weak(&last->next, &expected_next, n)) {
                    (void)atomic_compare_exchange_weak(&Q->tail, &last, n);
                    mm_op_end(Q, thread_id);
                    return 1;
                }
            } else {
                (void)atomic_compare_exchange_weak(&Q->tail, &last, next); //Helping out the enqueuers
            }
        }
    }
    mm_op_end(Q, thread_id);
    return 0;
}

int deq(value_t *v, queue_t* Q, int thread_id)
{
    mm_op_begin(Q, thread_id);
    while (true)
    {
        node_t* first = atomic_load(&Q->head);
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&first->next);

        if (first == atomic_load(&Q->head)) {
            if (first == last) {
                if (next == NULL) {
                    mm_op_end(Q, thread_id);
                    return 0;
                }
                (void)atomic_compare_exchange_weak(&Q->tail, &last, next); //Helping out the inquisitors
            } else {
                value_t val = next->data;
                if (atomic_compare_exchange_weak(&Q->head, &first, next)) {
                    *v = val;
                    mm_retire(Q, thread_id, first);
                    mm_op_end(Q, thread_id);
                    return 1;
                }
            }
        }
    }
    mm_op_end(Q, thread_id);
    return 0;
}

int deq_pause_before_cas(value_t* v, queue_t* Q, int thread_id,
                         _Atomic int* ready, _Atomic int* go,
                         node_t** observed_first, node_t** observed_next)
{
    mm_op_begin(Q, thread_id);
    while (true) {
        node_t* first = atomic_load(&Q->head);
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&first->next);

        if (first == atomic_load(&Q->head)) {
            if (first == last) {
                if (next == NULL) {
                    mm_op_end(Q, thread_id);
                    return 0;
                }
                (void)atomic_compare_exchange_weak(&Q->tail, &last, next);
            } else {
                value_t val = next->data;

                if (ready && atomic_load_explicit(ready, memory_order_relaxed) == 0) {
                    if (observed_first) *observed_first = first;
                    if (observed_next) *observed_next = next;
                    atomic_store_explicit(ready, 1, memory_order_release);
                }

                if (go) {
                    while (atomic_load_explicit(go, memory_order_acquire) == 0) {
                        // busy-wait
                    }
                }

                if (atomic_compare_exchange_weak(&Q->head, &first, next)) {
                    *v = val;
                    mm_retire(Q, thread_id, first);
                    mm_op_end(Q, thread_id);
                    return 1;
                }
            }
        }
    }
    mm_op_end(Q, thread_id);
    return 0;
}