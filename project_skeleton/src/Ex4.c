#include <Ex4.h>

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
    Q->num_threads = num_threads;
    Q->plock = (peterson_lock_t){ .flag = {false, false}, .victim = 0 };
    omp_init_lock(&Q->enq_lock);
    omp_init_lock(&Q->deq_lock);
    return;
}

void lock_peterson(peterson_lock_t* plock, uint8_t flag_id)
{
    // function_id: 0 for enqueue, 1 for dequeue
    uint8_t other_id = 1 - flag_id;
    plock->flag[flag_id] = true;
    plock->victim = flag_id;
    while (plock->flag[other_id] && plock->victim == flag_id) {
        // busy wait
    }
    return;
}
void unlock_peterson(peterson_lock_t* plock, uint8_t flag_id)
{
    plock->flag[flag_id] = false;
    return;
}

void destroy_queue(queue_t* Q)
{
    node_t* next;

    /* free main queue nodes */
    while (Q->head != NULL) {
        next = Q->head->next;
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
    omp_destroy_lock(&Q->enq_lock);
    omp_destroy_lock(&Q->deq_lock);
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
    //Locking to ensure thread safety during enqueue
    omp_set_lock(&Q->enq_lock);

    // Resolve ABA by waiting if queue has two nodes and is locked
    bool aba_wait = false;
    if(Q->head->next == Q->tail) {
        aba_wait = true;
        lock_peterson(&Q->plock, 0);
    };
    
    node_t* new_node = upcylce_node(Q, thread_id);
    if(!new_node){
        if(aba_wait) {
            unlock_peterson(&Q->plock, 0);
        }
        omp_unset_lock(&Q->enq_lock);
        return 0;
    }

    new_node->data = v;
    new_node->next = NULL;
    Q->tail->next = new_node;
    Q->tail = new_node;

    if(aba_wait) {
        unlock_peterson(&Q->plock, 0);
    }
    omp_unset_lock(&Q->enq_lock);

    return 1;
}

int deq(value_t *v, queue_t* Q, int thread_id)
{
    // Locking to ensure thread safety during dequeue
    omp_set_lock(&Q->deq_lock);
    // Resolve ABA by waiting if queue has two nodes and is locked
    bool aba_wait = false;
    if(Q->head->next == Q->tail) {
        aba_wait = true;
        lock_peterson(&Q->plock, 1);
    };

    if(!Q->head->next){
        if(aba_wait) {
            unlock_peterson(&Q->plock, 1);
        }
        omp_unset_lock(&Q->deq_lock);
        return 0;
    }

    node_t* sentinel = Q->head;
    *v = sentinel->next->data;
    Q->head = sentinel->next;
    if(Q->tail == sentinel->next){
        Q->tail = Q->head;
    }
    recycle_node(Q, thread_id, sentinel);
    if(aba_wait) {
        unlock_peterson(&Q->plock, 1);
    }
    omp_unset_lock(&Q->deq_lock);

    return 1;
}