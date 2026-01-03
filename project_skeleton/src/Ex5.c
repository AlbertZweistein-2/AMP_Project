#include <Ex5.h>

node_t* make_node(value_t v)
{
    node_t* new_node = malloc(sizeof(node_t));
    new_node->data = v;
    atomic_init(&new_node->next, pack(NULL, 0));
    return new_node;
}

void init_queue(queue_t* Q, int num_threads)
{
    atomic_init(&Q->head, make_node(0));
    atomic_init(&Q->tail, Q->head);
    Q->free_lists = calloc(num_threads, sizeof(freelist_t));
    Q->num_threads = num_threads;
    return;
}

void destroy_queue(queue_t* Q)
{
    node_t* next;

    /* free main queue nodes */
    while (Q->head != NULL) {
        next = atomic_load(Q->head->next);
        free(Q->head);
        Q->head = next;
    }

    /* free nodes stored in each thread-local free list */
    if (Q->free_lists) {
        for (int i = 0; i < Q->num_threads; ++i) {
            node_t* n = Q->free_lists[i].head;
            while (n != NULL) {
                node_t* tmp = n->next;
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
        Q->free_lists[tid].head = n->next;
        return n;
    }
    return malloc(sizeof(node_t));
}

// push in local free list -> use in dequeue
void recycle_node(queue_t* Q, int tid, node_t* node) 
{
    node->next = Q->free_lists[tid].head;
    Q->free_lists[tid].head = node;
    return;
}

int enq(value_t v, queue_t* Q, int thread_id)
{

    node_t* new_node = upcylce_node(Q, thread_id);
    while (true)
    {
        node_t* last = atomic_load(&Q->tail);
        node_t* next = atomic_load(&last->next);
        if (last == atomic_load(&Q->tail))
        {
            if (next == NULL)
            {
                if (atomic_compare_exchange_strong(&last->next, &next, new_node))
                {
                    atomic_compare_exchange_strong(&Q->tail, &last, new_node);
                    return 1;
                }
            } else {
                atomic_compare_exchange_strong(&Q->tail, &last, next);
            }
        }
    }
}

uint16_t getStamp(atomic_stamped_ptr_t addr)
{
    return unpack_stamp(atomic_load(&addr));
}

node_t* getReference(atomic_stamped_ptr_t addr)
{
    return unpack_ptr(atomic_load(&addr));
}

node_t* get(atomic_stamped_ptr_t addr, int* stamp_holder)
{
    stamp_holder[0] = getStamp(addr);
    return getReference(addr);
}

int deq(value_t *v, queue_t* Q, int thread_id)
{
    int* last_stamp = malloc(sizeof(int));
    int* first_stamp = malloc(sizeof(int));
    int* next_stamp = malloc(sizeof(int));
    while (true)
    {
        node_t* first = get(&Q->head, first_stamp);
        node_t* last = get(&Q->tail, last_stamp);
        node_t* next = get(&first->next, next_stamp);
        if (getStamp(&Q->head) == first_stamp[0])
        {
            if (first == last)
            {
                if (next == NULL)
                {
                    return 0; // queue is empty
                }
                compare_and_set_stamped(&Q->tail, &last, next, last_stamp[0], last_stamp[0] + 1);
            } else {
                value_t val = next->data;
                if (compare_and_set_stamped(&Q->head, &first, next, first_stamp[0], first_stamp[0] + 1))
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