#include <Ex5.h>

#define ATOMIC_STAMPED_PTR_NULL pack(NULL, 0)

node_t* make_node(value_t v)
{
    node_t* new_node = malloc(sizeof(node_t));
    new_node->data = v;
    atomic_init(&new_node->next, ATOMIC_STAMPED_PTR_NULL);
    return new_node;
}

node_t* get_next(node_t* n)
{
    return (node_t*) unpack_ptr(atomic_load(&n->next));
}


void init_queue(queue_t* Q, int num_threads)
{

    uint64_t dummy = pack(make_node(0), 0);
    atomic_init(&Q->head, dummy);
    atomic_init(&Q->tail, dummy);

    Q->free_lists = calloc(num_threads, sizeof(freelist_t));
    Q->num_threads = num_threads;
    return;
}

void destroy_queue(queue_t* Q)
{
    node_t* next;

    /* free main queue nodes */
    
    for (node_t* head_ptr = (node_t*) unpack_ptr(atomic_load(&Q->head)); head_ptr != NULL;) {
        node_t* next = get_next(head_ptr);
        free(head_ptr);
        head_ptr = next;
    }

    /* free nodes stored in each thread-local free list */
    if (Q->free_lists) {
        for (int i = 0; i < Q->num_threads; ++i) {
            node_t* n = Q->free_lists[i].head;
            while (n != NULL) {
                node_t* tmp = get_next(n);
                free(n);
                n = tmp;
            }
        }
        free(Q->free_lists);
        Q->free_lists = NULL;
        Q->num_threads = 0;
    }
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
    atomic_init(&n->next, ATOMIC_STAMPED_PTR_NULL);
    return n;
}

// push in local free list -> use in dequeue
void recycle_node(queue_t* Q, int tid, node_t* node) 
{
    atomic_store(&node->next, pack(Q->free_lists[tid].head, 0));
    Q->free_lists[tid].head = node;
    return;
}

int enq(value_t v, queue_t* Q, int thread_id)
{

    node_t* n = upcylce_node(Q, thread_id);
    n->data = v;
    atomic_store(&n->next, ATOMIC_STAMPED_PTR_NULL);

    while (true)
    {
        uint16_t last_stamp;
        node_t* last = get(&Q->tail, &last_stamp);

        uint16_t next_stamp;
        node_t* next = get(&last->next, &next_stamp);

        if (getStamp(&Q->tail) == last_stamp)
        {
            if (next == NULL)
            {
                if (compare_and_set_stamped(&last->next, NULL, n, next_stamp, next_stamp + 1))
                {
                    compare_and_set_stamped(&Q->tail, last, n, last_stamp, last_stamp + 1);
                    return 1;
                }
            } else {
                compare_and_set_stamped(&Q->tail, last, next, last_stamp, last_stamp + 1);
            }
        }
    }
}

uint16_t getStamp(atomic_stamped_ptr_t* addr)
{
    return unpack_stamp(atomic_load(addr));
}

node_t* getReference(atomic_stamped_ptr_t* addr)
{
    return unpack_ptr(atomic_load(addr));
}

node_t* get(atomic_stamped_ptr_t* addr, uint16_t* stamp_holder)
{
    uint64_t addr_value = atomic_load(addr);
    *stamp_holder = unpack_stamp(addr_value);
    return unpack_ptr(addr_value);
}

int deq(value_t *v, queue_t* Q, int thread_id)
{
    //Why not use stack integers?
    // int* last_stamp = malloc(sizeof(int));
    // int* first_stamp = malloc(sizeof(int));
    // int* next_stamp = malloc(sizeof(int));
    uint16_t first_stamp;
    uint16_t next_stamp;
    uint16_t last_stamp;
    
    while (true)
    {
        node_t* first = get(&Q->head, &first_stamp);
        node_t* last = get(&Q->tail, &last_stamp);
        node_t* next = get(&first->next, &next_stamp);
        if (getStamp(&Q->head) == first_stamp)
        {
            if (first == last)
            {
                if (next == NULL)
                {
                    return 0; // queue is empty
                }
                compare_and_set_stamped(&Q->tail, last, next, last_stamp, last_stamp + 1);
            } else {
                value_t val = next->data;
                if (compare_and_set_stamped(&Q->head, first, next, first_stamp, first_stamp + 1))
                {
                    *v = val;
                    recycle_node(Q, thread_id, first);
                    return 1;
                }
            }
        }
    }
    return 0;
}