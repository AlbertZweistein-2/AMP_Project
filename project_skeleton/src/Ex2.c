#include <Ex2.h>

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
    for(int i = 0; i < num_threads; i++) 
    {
        Q->free_lists[i].head = NULL;
        Q->free_lists[i].size = 0;
    }
    Q->num_threads = num_threads;
    omp_init_lock(&Q->lock);
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
    omp_destroy_lock(&Q->lock);
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
    omp_set_lock(&Q->lock);
    node_t* new_node = upcylce_node(Q, thread_id);
    if(!new_node)
    {
        omp_unset_lock(&Q->lock);
        return 0;
    }
    new_node->data = v;
    new_node->next = NULL;
    Q->tail->next = new_node;
    Q->tail = new_node;
    omp_unset_lock(&Q->lock);
    return 1;
}

int deq(value_t *v, queue_t* Q, int thread_id, int* free_list_insertion_count)
{
    // Lock
    omp_set_lock(&Q->lock);
    if(!Q->head->next)
    {
        omp_unset_lock(&Q->lock);
        return 0;
    }
    node_t* sentinel = Q->head;
    *v = sentinel->next->data;
    Q->head = sentinel->next;
    if(Q->tail == sentinel->next)
    {
        Q->tail = Q->head;
    }
    recycle_node(Q, thread_id, sentinel, free_list_insertion_count);
    omp_unset_lock(&Q->lock);
    return 1;
}