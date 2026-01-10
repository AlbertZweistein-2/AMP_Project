#include "Ex5.h"

static inline int is_odd(uint64_t x) 
{
    return (int)(x & 1ULL);
}

static inline void mm_op_begin(queue_t* Q, int tid) 
{
    // even -> odd (thread is inside a queue operation)
    atomic_fetch_add_explicit(&Q->ctr[tid], 1, memory_order_seq_cst);
    return;
}

static inline void mm_wait_until_unreserved(queue_t* Q, int tid) 
{
    for (int t = 0; t < Q->num_threads; t++) 
    {
        if (t == tid) continue;

        uint64_t v = atomic_load_explicit(&Q->ctr[t], memory_order_seq_cst);

        // Even => thread is quiescent already
        if (!is_odd(v)) continue;

        // Odd => wait until the counter changes
        while (atomic_load_explicit(&Q->ctr[t], memory_order_seq_cst) == v) 
        {// busy-wait
        }
    }
    return;
}

static inline void mm_reclaim_all(queue_t* Q, int tid, int* free_list_insertion_count) 
{
    retired_t* r = Q->retired[tid];
    Q->retired[tid] = NULL;
    Q->retired_count[tid] = 0;

    while (r != NULL) 
    {
        retired_t* nxt = r->next;
        recycle_node(Q, tid, r->node, free_list_insertion_count);
        free(r);
        r = nxt;
    }
    return;
}

static inline void mm_op_end(queue_t* Q, int tid, int* free_list_insertion_count) 
{
    // odd -> even (thread is outside a queue operation)
    atomic_fetch_add_explicit(&Q->ctr[tid], 1, memory_order_seq_cst);

    // Book-style (The Art of Multiprocessor Programming, Fig. 19.8):
    // reclaim all pending retired nodes at op_end().
    if (Q->retired_count[tid] == 0)
        return;

    mm_wait_until_unreserved(Q, tid);
    mm_reclaim_all(Q, tid, free_list_insertion_count);
    return;
}

static inline void mm_retire(queue_t* Q, int tid, node_t* n) 
{
    retired_t* r = malloc(sizeof(*r));
    r->node = n;
    r->next = Q->retired[tid];
    Q->retired[tid] = r;
    Q->retired_count[tid]++;
    return;
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
    //set free list sizes = 0
    for (int i = 0; i < num_threads; i++) 
    {
        Q->free_lists[i].head = NULL;
        Q->free_lists[i].size = 0;
    }
    Q->num_threads = num_threads;

    Q->ctr = calloc(num_threads, sizeof(_Atomic uint64_t));
    Q->retired = calloc(num_threads, sizeof(retired_t*));
    Q->retired_count = calloc(num_threads, sizeof(size_t));

    for (int i = 0; i < num_threads; i++) 
    {
        atomic_init(&Q->ctr[i], 0);
        Q->retired[i] = NULL;
        Q->retired_count[i] = 0;
    }
    return;
}

void destroy_queue(queue_t* Q)
{
    int nthreads = Q->num_threads;

    /* free main queue nodes */
    node_t* queue_head = (node_t*)atomic_load(&Q->head);
    while (queue_head != NULL) 
    {
        node_t* next = get_next(queue_head);
        free(queue_head);
        queue_head = next;
    }

    /* free nodes stored in each thread-local free list */
    if (Q->free_lists) 
    {
        for (int i = 0; i < nthreads; i++) 
        {
            node_t* freelists_head = Q->free_lists[i].head;
            while (freelists_head != NULL) 
            {
                node_t* next = get_next(freelists_head);
                free(freelists_head);
                freelists_head = next;
            }
        }
        free(Q->free_lists);
        Q->free_lists = NULL;
    }

    /* free retired nodes (not yet reclaimed into any freelist) */
    if (Q->retired) 
    {
        for (int i = 0; i < nthreads; i++) 
        {
            retired_t* r = Q->retired[i];
            while (r != NULL) 
            {
                retired_t* next = r->next;
                free(r->node);
                free(r);
                r = next;
            }
        }
        free(Q->retired);
        Q->retired = NULL;
    }

    if (Q->ctr) 
    {
        free(Q->ctr);
        Q->ctr = NULL;
    }

    if (Q->retired_count)
    {
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
    if (n != NULL) 
    {
        Q->free_lists[tid].head = get_next(n);
        //Update size of free list
        Q->free_lists[tid].size--;
        return n;
    }

    return make_node(0);
}

// push in local free list -> use in dequeue
void recycle_node(queue_t* Q, int tid, node_t* node, int* free_list_insertion_count) 
{
    atomic_store(&node->next, Q->free_lists[tid].head);
    Q->free_lists[tid].head = node;
    //Update size of free list
    Q->free_lists[tid].size++;
    if(free_list_insertion_count) (*free_list_insertion_count)++;
    return;
}

int enq(value_t v, queue_t* Q, int thread_id, int* failed_CAS_count, int* free_list_insertion_count)
{
    mm_op_begin(Q, thread_id);

    node_t* n = upcylce_node(Q, thread_id);
    n->data = v;
    atomic_store(&n->next, NULL);

    for(;;)
    {
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&last->next);

        if (last == atomic_load(&Q->tail)) 
        {
            if (next == NULL) 
            {
                node_t* expected_next = NULL;
                if (atomic_compare_exchange_weak(&last->next, &expected_next, n)) 
                {
                    if(!atomic_compare_exchange_weak(&Q->tail, &last, n))
                        if (failed_CAS_count) (*failed_CAS_count)++;

                    mm_op_end(Q, thread_id, free_list_insertion_count);
                    return 1;
                }
                else 
                    if(failed_CAS_count) (*failed_CAS_count)++;
            } 
            
            else 
            {
                if(!atomic_compare_exchange_weak(&Q->tail, &last, next))
                    if(failed_CAS_count) (*failed_CAS_count)++;
            }
        }
    }

    mm_op_end(Q, thread_id, free_list_insertion_count);
    return 0;
}

int deq(value_t *v, queue_t* Q, int thread_id, int* failed_CAS_count, int* free_list_insertion_count)
{
    mm_op_begin(Q, thread_id);

    for(;;)
    {
        node_t* first = atomic_load(&Q->head);
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&first->next);

        if (first == atomic_load(&Q->head)) 
        {
            if (first == last) 
            {
                if (next == NULL) 
                {
                    mm_op_end(Q, thread_id, free_list_insertion_count);
                    return 0;
                }
                if(!atomic_compare_exchange_weak(&Q->tail, &last, next))
                    if(failed_CAS_count) (*failed_CAS_count)++;
            } 
            
            else 
            {
                value_t val = next->data;
                if (atomic_compare_exchange_weak(&Q->head, &first, next)) 
                {
                    *v = val;
                    mm_retire(Q, thread_id, first);
                    mm_op_end(Q, thread_id, free_list_insertion_count);
                    return 1;
                }
                else
                    if(failed_CAS_count) (*failed_CAS_count)++;
            }
        }
    }

    mm_op_end(Q, thread_id, free_list_insertion_count);
    return 0;
}