#include "Ex4.h"

node_t* make_node(value_t v)
{
    node_t* new_node = malloc(sizeof(node_t));
    new_node->data = v;
    new_node->next = NULL;
    return new_node;
}

void init_queue(queue_t* Q, int num_threads)
{
    Q->head = make_node(0);
    Q->tail = Q->head;
    Q->free_lists = calloc(num_threads, sizeof(freelist_t));
    // Initialize each thread-local free list
    for (int i = 0; i < num_threads; ++i) 
    {
        Q->free_lists[i].head = NULL;
        Q->free_lists[i].size = 0;
    }
    Q->num_threads = num_threads;
    omp_init_lock(&Q->enq_lock);
    omp_init_lock(&Q->deq_lock);
    return;
}

void destroy_queue(queue_t* Q)
{
    node_t* next;

    /* free main queue nodes */
    while (Q->head != NULL) 
    {
        next = Q->head->next;
        free(Q->head);
        Q->head = next;
    }

    /* free nodes stored in each thread-local free list */
    if (Q->free_lists) 
    {
        for (int i = 0; i < Q->num_threads; ++i) 
        {
            node_t* n = Q->free_lists[i].head;
            while (n != NULL) 
            {
                node_t* tmp = n->next;
                free(n);
                n = tmp;
            }
        }
        free(Q->free_lists);
        Q->free_lists = NULL;
        Q->num_threads = 0;
    }
    omp_destroy_lock(&Q->enq_lock);
    omp_destroy_lock(&Q->deq_lock);
    return;
}

// pull from local free list -> use in enqueue
node_t* upcylce_node(queue_t* Q, int tid) 
{
    node_t* n = Q->free_lists[tid].head;
    if (n != NULL) 
    {
        Q->free_lists[tid].head = n->next;
        Q->free_lists[tid].size--;
        return n;
    }

    return malloc(sizeof(node_t));
}

// push in local free list -> use in dequeue
void recycle_node(queue_t* Q, int tid, node_t* node, int* free_list_insertion_count) 
{
    node->next = Q->free_lists[tid].head;
    Q->free_lists[tid].head = node;
    Q->free_lists[tid].size++;
    if(free_list_insertion_count) (*free_list_insertion_count)++;
    return;
}

int enq(value_t v, queue_t* Q, int thread_id)
{
    //Locking to ensure thread safety during enqueue
    omp_set_lock(&Q->enq_lock);
    
    node_t* new_node = upcylce_node(Q, thread_id);
    if(!new_node)
    {
        omp_unset_lock(&Q->enq_lock);
        return 0;
    }

    new_node->data = v;
    new_node->next = NULL;
    Q->tail->next = new_node;
    Q->tail = new_node;
    omp_unset_lock(&Q->enq_lock);

    return 1;
}

int deq(value_t *v, queue_t* Q, int thread_id, int* free_list_insertion_count)
{
    // Locking to ensure thread safety during dequeue
    omp_set_lock(&Q->deq_lock);

    node_t* sentinel = Q->head;
    node_t* next = sentinel->next;
    if(!next)
    {
        omp_unset_lock(&Q->deq_lock);
        return 0;
    }

    *v = next->data;
    Q->head = next;

    // If we removed the last real node, the new dummy head has next == NULL.
    // In that edge case, we must also move tail to the new dummy under enq_lock.
    if (Q->head->next == NULL)
    {
        omp_set_lock(&Q->enq_lock);
        // Another thread may have enqueued while we were waiting for enq_lock.
        // Re-check the condition under enq_lock before touching tail.
        if (Q->head->next == NULL)
            Q->tail = Q->head;
        omp_unset_lock(&Q->enq_lock);
    }

    recycle_node(Q, thread_id, sentinel, free_list_insertion_count);

    omp_unset_lock(&Q->deq_lock);

    return 1;
}