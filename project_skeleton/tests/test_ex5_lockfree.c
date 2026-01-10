#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <omp.h>

#include "../src/Ex5.h"

// Helper function for is_odd (same as in Ex5.c)
static inline int test_is_odd(uint64_t x) {
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
        if (!test_is_odd(v)) continue;

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

// Test helper: pauses just before attempting the head CAS in the dequeue fast-path.
// Used to force ABA-style interleavings in unit tests.
// `ready` is set to 1 after capturing (first,next); the caller sets `go` to 1 to resume.
static inline int deq_pause_before_cas(value_t* v, queue_t* Q, int thread_id,
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
                    mm_op_end(Q, thread_id, NULL);
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
                    mm_op_end(Q, thread_id, NULL);
                    return 1;
                }
            }
        }
    }
    mm_op_end(Q, thread_id, NULL);
    return 0;
}


// Helper to test initialization and basic operations
static void test_init_destroy() {
    printf("Test: init_destroy\n");
    queue_t q;
    init_queue(&q, 4);
    
    assert(atomic_load(&q.head) != NULL);
    assert(atomic_load(&q.tail) != NULL);
    assert(q.num_threads == 4);
    assert(q.free_lists != NULL);
    
    destroy_queue(&q);
    
    assert(q.free_lists == NULL);
    assert(q.num_threads == 0);
    
    printf("  PASS\n");
}

// Test FIFO ordering: enqueue and dequeue basic values with single thread
static void test_fifo_order() {
    printf("Test: fifo_order\n");
    queue_t q;
    int thread_id = 0;
    init_queue(&q, 1);
    
    // Enqueue values 10, 20, 30
    int ret1 = enq(10, &q, thread_id, NULL, NULL);
    int ret2 = enq(20, &q, thread_id, NULL, NULL);
    int ret3 = enq(30, &q, thread_id, NULL, NULL);
    
    assert(ret1 == 1);
    assert(ret2 == 1);
    assert(ret3 == 1);
    
    // Dequeue and verify FIFO order
    value_t v1, v2, v3;
    int deq1 = deq(&v1, &q, thread_id, NULL, NULL);
    int deq2 = deq(&v2, &q, thread_id, NULL, NULL);
    int deq3 = deq(&v3, &q, thread_id, NULL, NULL);
    
    assert(deq1 == 1 && v1 == 10);
    assert(deq2 == 1 && v2 == 20);
    assert(deq3 == 1 && v3 == 30);
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// Test dequeue from empty queue
static void test_dequeue_empty() {
    printf("Test: dequeue_empty\n");
    queue_t q;
    int thread_id = 0;
    init_queue(&q, 1);
    
    value_t v;
    int ret = deq(&v, &q, thread_id, NULL, NULL);
      
    // Should return 0 (fail) because only sentinel node exists
    assert(ret == 0);
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// Test multiple threads with thread-local free lists
static void test_single_thread_multi_freelists() {
    printf("Test: multi_thread_freelists\n");
    queue_t q;
    init_queue(&q, 4);
    
    // Thread 0: enqueue/dequeue
    int ret1 = enq(100, &q, 0, NULL, NULL);
    value_t v1;
    int ret2 = deq(&v1, &q, 0, NULL, NULL);
    assert(ret1 == 1 && ret2 == 1 && v1 == 100);
    
    // Thread 1: enqueue/dequeue
    int ret3 = enq(200, &q, 1, NULL, NULL);
    value_t v2;
    int ret4 = deq(&v2, &q, 1, NULL, NULL);
    assert(ret3 == 1 && ret4 == 1 && v2 == 200);
    
    // Thread 2: enqueue/dequeue
    int ret5 = enq(300, &q, 2, NULL, NULL);
    value_t v3;
    int ret6 = deq(&v3, &q, 2, NULL, NULL);
    assert(ret5 == 1 && ret6 == 1 && v3 == 300);
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// Test node recycling: dequeued nodes are reused from free list
static void test_node_recycling() {
    printf("Test: node_recycling\n");
    queue_t q;

    // Book-style QSBR: retired nodes are reclaimed at op_end().
    int tid = 0;
    init_queue(&q, 1);

    value_t v;

    enq(111, &q, tid, NULL, NULL);
    node_t* first_retired = atomic_load(&q.head); // the old sentinel to be retired
    assert(deq(&v, &q, tid, NULL, NULL) == 1 && v == 111);

    // Reclamation should have moved the retired sentinel to the freelist.
    assert(q.free_lists[tid].head == first_retired);

    // Next enqueue should reuse the freelist head (oldest retired).
    enq(9999, &q, tid, NULL, NULL);
    assert(atomic_load(&q.tail) == first_retired);
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// Test alternating enqueue/dequeue
static void test_mixed_operations() {
    printf("Test: mixed_operations\n");
    queue_t q;
    int thread_id = 0;
    init_queue(&q, 1);
    
    // Enqueue 1, enqueue 2, dequeue (should be 1), enqueue 3, dequeue (should be 2)
    enq(1, &q, thread_id, NULL, NULL);
    enq(2, &q, thread_id, NULL, NULL);
    
    value_t v1;
    assert(deq(&v1, &q, thread_id, NULL, NULL) == 1 && v1 == 1);
    
    enq(3, &q, thread_id, NULL, NULL);
    
    value_t v2;
    assert(deq(&v2, &q, thread_id, NULL, NULL) == 1 && v2 == 2);
    
    value_t v3;
    assert(deq(&v3, &q, thread_id, NULL, NULL) == 1 && v3 == 3);
    
    destroy_queue(&q);
    printf("  PASS\n");
}


//Correctness Test: Disjoint Intervals
static void test_disjoint_intervals() {
    printf("Test: disjoint_intervals\n");
    const int num_threads = 4;
    const int items_per_thread = 1000;
    //Define array that holds values form 0 to num_threads * items_per_thread -1
    value_t items[num_threads * items_per_thread];
    for (int i = 0; i < num_threads * items_per_thread; i++) {
        items[i] = (value_t)i;
    }
    
    queue_t q;
    init_queue(&q, num_threads);
    value_t total_enqueued = 0;
    value_t total_dequeued = 0;

    #pragma omp parallel num_threads(num_threads)
    {
        int thread_id = omp_get_thread_num();
        value_t* thread_items = calloc(items_per_thread, sizeof(value_t));
        memcpy(thread_items, &items[thread_id * items_per_thread], items_per_thread * sizeof(value_t));
        
        value_t local_enqueued = 0;
        value_t local_dequeued = 0;
        
        // Enqueue disjoint intervals
        for (int i = 0; i < items_per_thread; i++) {
            if (enq(thread_items[i], &q, thread_id, NULL, NULL) == 1){
                local_enqueued += thread_items[i];
            }
        }
        
        // Dequeue until empty
        value_t v;
        while (deq(&v, &q, thread_id, NULL, NULL)) {
            local_dequeued += v;
        }
        
        #pragma omp atomic
        total_enqueued += local_enqueued;
        
        #pragma omp atomic
        total_dequeued += local_dequeued;
    }

    assert(total_enqueued == total_dequeued);
    destroy_queue(&q);
    printf("  PASS\n");
}

static void test_ABA_problem() {
    printf("Test: ABA_problem\n");

    // This test forces an ABA-like schedule and checks that EBR/QSBR prevents it.
    // Thread A pauses after reading (first=a, next=b) and before attempting head CAS.
    // Thread B dequeues once, which advances head away from a and retires a.
    // With our book-style QSBR (reclaim-at-op_end), B cannot reclaim/reuse a while A is
    // still "in operation". So head must never return to a before A resumes.
    queue_t q;
    init_queue(&q, 2);

    // Create at least two real nodes (b,c) after sentinel a.
    assert(enq(1, &q, 0, NULL, NULL) == 1);
    assert(enq(2, &q, 0, NULL, NULL) == 1);

    _Atomic int ready = 0;
    _Atomic int go = 0;
    _Atomic int head_moved = 0;
    _Atomic int returned_to_a = 0;
    _Atomic int retired_list_ok = 0;

    node_t* observed_first = NULL;
    node_t* observed_next = NULL;

    value_t va = -1;
    value_t vb = -1;
    _Atomic int ra = 0;
    _Atomic int rb = 0;

    #pragma omp parallel num_threads(3) shared(q, ready, go, head_moved, returned_to_a, retired_list_ok, observed_first, observed_next, va, vb, ra, rb)
    {
        int tid = omp_get_thread_num();
        if (tid == 0) {
            int r = deq_pause_before_cas(&va, &q, 0, &ready, &go, &observed_first, &observed_next);
            atomic_store_explicit(&ra, r, memory_order_release);
        } else if (tid == 1) {
            while (atomic_load_explicit(&ready, memory_order_acquire) == 0) {
                // wait until A captured (a,b)
            }
            int r = deq(&vb, &q, 1, NULL, NULL);
            atomic_store_explicit(&rb, r, memory_order_release);
        } else {
            // Observer thread (does not call queue ops; just watches head).
            while (atomic_load_explicit(&ready, memory_order_acquire) == 0) 
            { 
                // wait until A captured (a,b)
            }
            node_t* a = observed_first;
            node_t* b = observed_next;

            // Wait until head changes away from the old sentinel address.
            while (atomic_load_explicit(&q.head, memory_order_seq_cst) == a) {
                // B should eventually CAS head from a -> b
            }

            if (atomic_load_explicit(&q.head, memory_order_seq_cst) == b) {
                atomic_store_explicit(&head_moved, 1, memory_order_release);
            }

            // While A is paused (go==0), thread 1's deq() should have retired `a` but
            // must be blocked in mm_wait_until_unreserved() (because ctr[0] is odd).
            // We use ctr[1] (seq_cst) as a synchronization point to safely read q.retired[1].
            while ((atomic_load_explicit(&q.ctr[1], memory_order_seq_cst) & 1ULL) != 0ULL) {
                // wait until thread 1 reached mm_op_end() (odd -> even)
            }

            // Verify retired list of thread 1 contains exactly the expected node `a`.
            int ok = 0;
            retired_t* r = q.retired[1];
            if (r && r->node == a && r->next == NULL) {
                ok = 1;
            }
            atomic_store_explicit(&retired_list_ok, ok, memory_order_release);

            // While A is still paused, head must not return to a.
            // If ABA were possible via reclamation+reuse, head could become a again here.
            /*while (atomic_load_explicit(&go, memory_order_acquire) == 0) {
                if (atomic_load_explicit(&q.head, memory_order_seq_cst) == b) {
                    atomic_store_explicit(&returned_to_a, 1, memory_order_release);
                    break;
                }
            }*/

            // Let A resume.
            atomic_store_explicit(&go, 1, memory_order_release);
        }
    }

    //Checking if the free node in thread A's free list is the same as observed_next,
    //Because Thread B should have retired node b (which was observed_first by thread A)
    node_t* free_node_A = q.free_lists[0].head;
    //printf("Retired node A address: %p, observed_next address: %p\n", (void*)retired_node_A, (void*)observed_next);
    assert(free_node_A != NULL && free_node_A == observed_next);

    //Check if free node in thread B's free list is the same as observed_first,
    //Because Thread B should have retired node a (which was observed_first by thread A)
    node_t* free_node_B = q.free_lists[1].head;
    //printf("Retired node B address: %p, observed_first address: %p\n", (void*)retired_node_B, (void*)observed_first);
    assert(free_node_B != NULL && free_node_B == observed_first);

    assert(atomic_load_explicit(&head_moved, memory_order_acquire) == 1);
    assert(atomic_load_explicit(&returned_to_a, memory_order_acquire) == 0);
    assert(atomic_load_explicit(&retired_list_ok, memory_order_acquire) == 1);
    assert(atomic_load_explicit(&ra, memory_order_acquire) == 1);
    assert(atomic_load_explicit(&rb, memory_order_acquire) == 1);
    assert((va == 1 && vb == 2) || (va == 2 && vb == 1));

    // Queue should be empty now.
    value_t v;
    assert(deq(&v, &q, 0, NULL, NULL) == 0);

    destroy_queue(&q);
    printf("  PASS\n");
}

// Main test runner
int main(void) {
    printf("========================================\n");
    printf("Running Ex5 LockFree Queue Unit Tests\n");
    printf("========================================\n\n");
    
    test_init_destroy();
    test_fifo_order();
    test_dequeue_empty();
    test_single_thread_multi_freelists();
    test_node_recycling();
    test_mixed_operations();
    test_ABA_problem();
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}