#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <omp.h>

#include "../src/Ex5.h"

// Helper to test initialization and basic operations
static void test_init_destroy() {
    printf("Test: init_destroy\n");
    queue_t q;
    init_queue(&q, 4);
    
    assert(unpack_ptr(q.head) != NULL);
    assert(unpack_ptr(q.tail) != NULL);
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
    int ret1 = enq(10, &q, thread_id);

    int ret2 = enq(20, &q, thread_id);

    int ret3 = enq(30, &q, thread_id);
    
    assert(ret1 == 1);
    assert(ret2 == 1);
    assert(ret3 == 1);
    
    // Dequeue and verify FIFO order
    value_t v1, v2, v3;
    int deq1 = deq(&v1, &q, thread_id);
    int deq2 = deq(&v2, &q, thread_id);
    int deq3 = deq(&v3, &q, thread_id);
    
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
    int ret = deq(&v, &q, thread_id);
      
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
    int ret1 = enq(100, &q, 0);
    value_t v1;
    node_t* ptr_temp = unpack_ptr(q.head);
    int ret2 = deq(&v1, &q, 0);
    assert(ret1 == 1 && ret2 == 1 && v1 == 100);
    assert(ptr_temp == q.free_lists[0].head); // Ensure free list was used
    
    // Thread 1: enqueue/dequeue
    int ret3 = enq(200, &q, 1);
    node_t* ptr_temp3 = unpack_ptr(q.head);
    value_t v2;
    int ret4 = deq(&v2, &q, 1);
    assert(ret3 == 1 && ret4 == 1 && v2 == 200);
    assert(ptr_temp3 == q.free_lists[1].head); // Ensure free list was used
    
    // Thread 2: enqueue/dequeue
    int ret5 = enq(300, &q, 2);
    node_t* ptr_temp4 = unpack_ptr(q.head);
    value_t v3;
    int ret6 = deq(&v3, &q, 2);
    assert(ret5 == 1 && ret6 == 1 && v3 == 300);
    assert(ptr_temp4 == q.free_lists[2].head); // Ensure free list was used
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// Test node recycling: dequeued nodes are reused from free list
static void test_node_recycling() {
    printf("Test: node_recycling\n");
    queue_t q;
    int thread_0_id = 0;
    int thread_1_id = 1;
    init_queue(&q, 2);
    
    // First cycle: enqueue and dequeue to populate free list
    enq(111, &q, thread_0_id);
    node_t* first_ptr = unpack_ptr(q.head);
    value_t v1;
    deq(&v1, &q, thread_0_id);  // This node should go into free_lists[0]
    
    enq(222, &q, thread_1_id);
    node_t* second_ptr = unpack_ptr(q.head);
    value_t v2;
    deq(&v2, &q, thread_1_id);  // This node should go into free_lists[1]

    // Second cycle: next enqueue should reuse the freed node
    // If free list works, the node from first dequeue is reused
    enq(112, &q, thread_0_id);
    enq(223, &q, thread_1_id);

    node_t* head = unpack_ptr(q.head);
    node_t* next1 = unpack_ptr(head->next);
    node_t* next2 = unpack_ptr(next1->next);
    assert(next1->data == 112);
    assert(next1 == first_ptr);
    assert(next2->data == 223);
    assert(next2 == second_ptr);
    
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
    enq(1, &q, thread_id);
    enq(2, &q, thread_id);
    
    value_t v1;
    assert(deq(&v1, &q, thread_id) == 1 && v1 == 1);
    
    enq(3, &q, thread_id);
    
    value_t v2;
    assert(deq(&v2, &q, thread_id) == 1 && v2 == 2);
    
    value_t v3;
    assert(deq(&v3, &q, thread_id) == 1 && v3 == 3);
    
    destroy_queue(&q);
    printf("  PASS\n");
}

// ABA Problem Test:
// Dieses Test simuliert das ABA-Szenario:
// 1. Knoten A wird aus der Queue entfernt (deq)
// 2. Knoten A wird recycled (kommt in free list)
// 3. Knoten A wird wieder eingefügt (enq) - SELBER POINTER!
// 4. Ein CAS mit dem alten Stempel sollte fehlschlagen
//
// Wenn Stamped Pointers korrekt implementiert sind:
// - Der Stempel am head/tail ändert sich bei jeder Operation
// - Auch wenn derselbe Pointer wiederverwendet wird, unterscheiden sich die Stempel
static void test_aba_problem_prevention() {
    printf("Test: aba_problem_prevention\n");
    queue_t q;
    int tid = 0;
    init_queue(&q, 1);
    
    // Initiale Stempel speichern
    uint16_t initial_head_stamp = unpack_stamp(atomic_load(&q.head));
    uint16_t initial_tail_stamp = unpack_stamp(atomic_load(&q.tail));
    
    // 1. Enqueue einen Wert - Tail-Stempel sollte sich ändern
    enq(42, &q, tid);
    uint16_t stamp_after_enq1 = unpack_stamp(atomic_load(&q.tail));
    assert(stamp_after_enq1 != initial_tail_stamp); // Stempel muss sich ändern!
    
    // Speichere den Pointer auf den eingefügten Knoten
    node_t* sentinel = unpack_ptr(atomic_load(&q.head));
    node_t* node_A = unpack_ptr(sentinel->next);
    
    // 2. Dequeue - Head-Stempel sollte sich ändern, Knoten wird recycled
    value_t v;
    deq(&v, &q, tid);
    assert(v == 42);
    uint16_t stamp_after_deq1 = unpack_stamp(atomic_load(&q.head));
    assert(stamp_after_deq1 != initial_head_stamp); // Stempel muss sich ändern!
    
    // Knoten A sollte jetzt in der free list sein
    assert(q.free_lists[tid].head == node_A);
    
    // 3. Enqueue erneut - Knoten A wird aus free list wiederverwendet (ABA!)
    enq(99, &q, tid);
    
    // Der SELBE Pointer wird wiederverwendet
    node_t* new_sentinel = unpack_ptr(atomic_load(&q.head));
    node_t* reused_node = unpack_ptr(new_sentinel->next);
    assert(reused_node == node_A); // Gleicher Pointer wie vorher! (A -> B -> A)
    
    // ABER: Die Stempel haben sich geändert!
    uint16_t stamp_after_enq2 = unpack_stamp(atomic_load(&q.tail));
    assert(stamp_after_enq2 != stamp_after_enq1); // Stempel unterscheidet A von A'
    
    // 4. Simuliere einen veralteten CAS-Versuch
    // Ein Thread der den alten Stempel gespeichert hat, sollte scheitern
    node_t* current_head = unpack_ptr(atomic_load(&q.head));
    node_t* next_node = unpack_ptr(current_head->next);
    
    // Versuche CAS mit ALTEM Stempel - sollte FEHLSCHLAGEN
    bool stale_cas_result = compare_and_set_stamped(
        &q.head,
        current_head,
        next_node,
        initial_head_stamp,  // Veralteter Stempel!
        initial_head_stamp + 1
    );
    assert(stale_cas_result == false); // MUSS fehlschlagen wegen ABA-Schutz!
    
    // Versuche CAS mit AKTUELLEM Stempel - sollte GELINGEN
    uint16_t current_stamp = unpack_stamp(atomic_load(&q.head));
    bool correct_cas_result = compare_and_set_stamped(
        &q.head,
        current_head,
        next_node,
        current_stamp,  // Aktueller Stempel
        current_stamp + 1
    );
    assert(correct_cas_result == true); // Muss gelingen!
    
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
            if (enq(thread_items[i], &q, thread_id) == 1){
                local_enqueued += thread_items[i];
            }
        }
        
        // Dequeue until empty
        value_t v;
        while (deq(&v, &q, thread_id)) {
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
    test_aba_problem_prevention();
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}