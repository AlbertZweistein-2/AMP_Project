#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "../src/Ex1.c"  // Include the implementation for testing

// Helper to test initialization and basic operations
static void test_init_destroy() {
    printf("Test: init_destroy\n");
    queue_t q;
    init_queue(&q, 4);
    
    assert(q.head != NULL);
    assert(q.tail != NULL);
    assert(q.num_threads == 4);
    assert(q.free_lists != NULL);
    
    destroy_queue(&q);
    
    assert(q.free_lists == NULL);
    assert(q.num_threads == 0);
    
    printf("  PASS\n");
}

// Test FIFO ordering: enqueue and dequeue basic values
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
static void test_multi_thread_freelists() {
    printf("Test: multi_thread_freelists\n");
    queue_t q;
    init_queue(&q, 4);
    
    // Thread 0: enqueue/dequeue
    int ret1 = enq(100, &q, 0);
    value_t v1;
    node_t* ptr_temp = q.head;
    int ret2 = deq(&v1, &q, 0);
    assert(ret1 == 1 && ret2 == 1 && v1 == 100);
    assert(ptr_temp == q.free_lists[0].head); // Ensure free list was used
    
    // Thread 1: enqueue/dequeue
    int ret3 = enq(200, &q, 1);
    node_t* ptr_temp3 = q.head;
    value_t v2;
    int ret4 = deq(&v2, &q, 1);
    assert(ret3 == 1 && ret4 == 1 && v2 == 200);
    assert(ptr_temp3 == q.free_lists[1].head); // Ensure free list was used
    
    // Thread 2: enqueue/dequeue
    int ret5 = enq(300, &q, 2);
    node_t* ptr_temp4 = q.head;
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
    node_t* first_ptr = q.head;
    value_t v1;
    deq(&v1, &q, thread_0_id);  // This node should go into free_lists[0]
    
    enq(222, &q, thread_1_id);
    node_t* second_ptr = q.head;
    value_t v2;
    deq(&v2, &q, thread_1_id);  // This node should go into free_lists[1]

    // Second cycle: next enqueue should reuse the freed node
    // If free list works, the node from first dequeue is reused
    enq(112, &q, thread_0_id);
    
    enq(223, &q, thread_1_id);

    assert(q.head->next->data == 112);
    assert(q.head->next == first_ptr);
    assert(q.head->next->next->data == 223);
    assert(q.head->next->next == second_ptr);
    
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

// Main test runner
int main(void) {
    printf("========================================\n");
    printf("Running Ex1 Queue Unit Tests\n");
    printf("========================================\n\n");
    
    test_init_destroy();
    test_fifo_order();
    test_dequeue_empty();
    test_multi_thread_freelists();
    test_node_recycling();
    test_mixed_operations();
    
    printf("\n========================================\n");
    printf("All tests passed!\n");
    printf("========================================\n");
    
    return 0;
}
