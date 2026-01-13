// Catherine Bley - 12002266
// Moritz Jasper Techen - 12432927
// Tobias Ponesch - 11818774

#include "../src/bench.h"

void run_benchmark(int num_threads, int time_interval,
                   batch_spec_t* enq_specs, batch_spec_t* deq_specs,
                   int Nr_enq_operations[], int Nr_deq_operations[],
                   int Nr_failed_enq_operations[],int Nr_failed_deq_operations[],
                   int Nr_failed_enq_CAS_operations[], int Nr_failed_deq_CAS_operations[],
                   int Nr_free_list_insertions[], int Max_free_list_size[], double Thread_times[])
{
    queue_t queue;
    init_queue(&queue, num_threads);
    #if DEBUG && VERSION == 5
        printf("Queue initialized.\n");
        assert(atomic_load(&queue.head) != NULL);
        assert(atomic_load(&queue.tail) != NULL);
        assert(queue.num_threads == num_threads);
        assert(queue.free_lists != NULL);
        assert(atomic_load(&queue.head) == atomic_load(&queue.tail));
        printf("Queue head and tail point to the same dummy node.\n");
    #elif DEBUG
        printf("Queue initialized.\n");
        assert(queue.head != NULL);
        assert(queue.tail != NULL);
        assert(queue.num_threads == num_threads);
        assert(queue.head == queue.tail);
        printf("Queue head and tail point to the same dummy node.\n");
    #endif

    #if CHECK_CORRECTNESS
        uint64_t total_dequeue_sum = 0;
    #endif

    #pragma omp parallel num_threads(num_threads)
    {
        int tid = omp_get_thread_num();
        unsigned int seed = (unsigned int)(time(NULL) ^ (tid * 0x9e3779b9u));
        int enq_count = 0;
        int deq_count = 0;
        int failed_enq_count = 0;
        int failed_deq_count = 0;
        int failed_enq_CAS_count = 0;
        int failed_deq_CAS_count = 0;
        int free_list_insertions = 0;
        int max_free_list_size = 0;
        #if CHECK_CORRECTNESS
            uint64_t dequeue_sum = 0;
        #endif

        // Measure the time taken for the exeriment per thread
        const double start_time = omp_get_wtime();
        // Each thread runs the benchmark for the specified time interval
        const double end_time = start_time + (double)time_interval;
        while (omp_get_wtime() < end_time)
        {
            int enq_batch_size = batch_spec_sample(&enq_specs[tid], &seed);
            int deq_batch_size = batch_spec_sample(&deq_specs[tid], &seed);
            unsigned int success;

            // Enqueue batch
            for (int i = 0; i < enq_batch_size; )
            {
                success = 0;
                value_t value = enq_count * num_threads + tid;
                #if VERSION == 5
                    success += enq(value, &queue, tid, &failed_enq_CAS_count, &free_list_insertions);
                #else
                    success += enq(value, &queue, tid);
                #endif
                max_free_list_size = queue.free_lists[tid].size > max_free_list_size ? queue.free_lists[tid].size : max_free_list_size;
                enq_count += success;
                failed_enq_count += (1 - success);
                i += success;
            }

            // Dequeue batch
            for (int i = 0; i < deq_batch_size; i++)
            {
                success = 0;
                value_t value;
                #if VERSION == 5
                    success = deq(&value, &queue, tid, &failed_deq_CAS_count, &free_list_insertions);
                #else
                    success = deq(&value, &queue, tid, &free_list_insertions);
                #endif

                max_free_list_size = queue.free_lists[tid].size > max_free_list_size ? queue.free_lists[tid].size : max_free_list_size;
                deq_count += success;
                failed_deq_count += (1 - success);

                #if CHECK_CORRECTNESS
                    if(success)
                        dequeue_sum += (uint64_t)value;
                #endif
            }
        }
        double thread_time = omp_get_wtime() - start_time;

        // Store thread-local counters into the provided arrays
        Nr_enq_operations[tid] = enq_count;
        Nr_deq_operations[tid] = deq_count;
        Nr_failed_enq_operations[tid] = failed_enq_count;
        Nr_failed_deq_operations[tid] = failed_deq_count;
        Nr_failed_enq_CAS_operations[tid] = failed_enq_CAS_count;
        Nr_failed_deq_CAS_operations[tid] = failed_deq_CAS_count;
        Nr_free_list_insertions[tid] = free_list_insertions;
        Max_free_list_size[tid] = max_free_list_size;
        Thread_times[tid] = thread_time;

        #if CHECK_CORRECTNESS
            #pragma omp atomic update
            total_dequeue_sum += dequeue_sum;
        #endif
    }

    #if CHECK_CORRECTNESS && VERSION == 5
        uint64_t expected_sum = 0;
        for (int tid = 0; tid < num_threads; tid++)
        {
            int enq_count = Nr_enq_operations[tid];
            for (int i = 0; i < enq_count; i++)
            {
                expected_sum += (uint64_t)i * (uint64_t)num_threads + (uint64_t)tid;
            }
        }

        value_t dv;
        for(;;)
        {
            int ok = deq(&dv, &queue, 0, NULL, NULL);
            if (!ok) break;
            total_dequeue_sum += (uint64_t)dv;
        }

        if (total_dequeue_sum != expected_sum)
            printf("Correctness check failed: expected sum %" PRIu64 ", got %" PRIu64 "\n", 
                expected_sum, total_dequeue_sum);
        else
            printf("Correctness check passed: total dequeue sum matches expected sum %" PRIu64 "\n", 
                expected_sum);
    #elif CHECK_CORRECTNESS
        uint64_t expected_sum = 0;
        for (int tid = 0; tid < num_threads; tid++)
        {
            int enq_count = Nr_enq_operations[tid];
            for (int i = 0; i < enq_count; i++)
            {
                expected_sum += (uint64_t)i * (uint64_t)num_threads + (uint64_t)tid;
            }
        }
        
        value_t dv;
        for(;;)
        {
            int ok = deq(&dv, &queue, 0, NULL);
            if (!ok) break;
            total_dequeue_sum += (uint64_t)dv;
        }

        if (total_dequeue_sum != expected_sum)
            printf("Correctness check failed: expected sum %" PRIu64 ", got %" PRIu64 "\n", 
                expected_sum, total_dequeue_sum);
        else
            printf("Correctness check passed: total dequeue sum matches expected sum %" PRIu64 "\n", 
                expected_sum);
    #endif
    destroy_queue(&queue);
}

void write_results_line(int rep, int num_threads, int time_interval,
                        int* Nr_enq_operations, int* Nr_deq_operations,
                        int* Nr_failed_enq_operations, int* Nr_failed_deq_operations,
                        int* Nr_failed_enq_CAS_operations, int* Nr_failed_deq_CAS_operations,
                        int* Nr_free_list_insertions, int* Max_free_list_size, double* Thread_times, char* filename)
{

    FILE *f = fopen(filename, "a");
    if (f == NULL)
    {
        perror("Error opening file for writing results");
        return;
    }

    // Write header if first repetition
    if (rep == 0)
    {
        fprintf(f, "Repetition,NumThreads,TimeInterval,ActualBenchTime,ThreadID,NrEnqOps,NrDeqOps,NrFailedEnqOps,NrFailedDeqOps,NrFailedEnqCASOps,NrFailedDeqCASOps,NrFreeListInsertions,MaxFreeListSize\n");
    }

    for (int tid = 0; tid < num_threads; tid++)
    {
        fprintf(f, "%d,%d,%d,%f,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                rep + 1,
                num_threads,
                time_interval,
                Thread_times[tid],
                tid,
                Nr_enq_operations[tid],
                Nr_deq_operations[tid],
                Nr_failed_enq_operations[tid],
                Nr_failed_deq_operations[tid],
                Nr_failed_enq_CAS_operations[tid],
                Nr_failed_deq_CAS_operations[tid],
                Nr_free_list_insertions[tid],
                Max_free_list_size[tid]);
    }
    fclose(f);
}

int main(int argc, char* argv[])
{
    // ------- Configuration parameters -------:
    // argv[1]: Number of threads
    // argv[2]: Number of repetitions
    // argv[3]: Time interval for throughput measurement
    // argv[4]: Enqueue batch size (per thread or according to some pattern)
    // argv[5]: Dequeue batch size (per thread or according to some pattern)
    // argv[6]: (optional) Output filename
    // -----------------------------------------
    // -------- Possible Batch Size Patterns --------
    // Batch size specification forms (passed as strings via argv[4]/argv[5]; whitespace is ignored inside tuples/lists):
    // 1) Fixed: integer value given as argument
    //    - Example: "10"  -> always use batch size 10
    //
    // 2) Random range: tuple "(min,max)"
    //    - Semantics: for EACH BATCH, draw a fresh random size uniformly from the inclusive range [min,max]
    //      using a per-thread RNG (rand_r()).
    //    - Example: "(0,10)" -> each batch chooses a size in 0..10 (inclusive)
    //
    // 3) Repeating per-thread pattern: list "[x0,x1,...]" where each xi is either an integer or a tuple "(min,max)"
    //    - Mapping: thread tid uses element x[tid % len(list)]
    //    - If x[...] is a tuple, it is (re-)sampled for EACH BATCH (same semantics as #2)
    //    - Example: "[5,10,(0,20)]"
    //      -> Thread 0 uses 5, Thread 1 uses 10, Thread 2 samples 0..20 each batch,
    //         Thread 3 uses 5, Thread 4 uses 10, Thread 5 samples 0..20 each batch, ...
    //
    // 4) Explicit per-thread list: "{x0,x1,...,xT-1}" (must match num_threads exactly)
    //    - Each xi can be an integer or a tuple "(min,max)" (tuples are re-sampled each batch).
    //    - Example: "{5,(0,10),10,20}"
    //      -> Thread 0 uses 5, Thread 1 samples 0..10 each batch, Thread 2 uses 10, Thread 3 uses 20
    // Notes / validation:
    // - Ranges are inclusive; 0 is allowed and means "do zero operations for that batch".
    // - Negative sizes are invalid; tuples require min <= max.
    // ---------------------------------------------

    int num_threads = 0;
    int num_repetitions = 0;
    int time_interval = 0;
    batch_spec_t* enq_specs = NULL;
    batch_spec_t* deq_specs = NULL;
    char* filename;

    parse_args(argc, argv, &num_threads, &num_repetitions, &time_interval, &enq_specs, &deq_specs, &filename);
    #if DEBUG
        print_configuration(num_threads, num_repetitions, time_interval, enq_specs, deq_specs);
    #endif
    #if VERSION == 1
        if (num_threads != 1)
        {
            printf("Warning: Version 1 only supports a single thread. Overriding num_threads to 1.\n");
            num_threads = 1;
        }
    #endif
    
    // Global Counters:
    // - Number of enqueue operations performed
    // - Number of dequeue operations performed
    // - Number of failed dequeue operations
    // - Number of (failed) CAS operations
    // - Number of elements inserted into the free lists
    // - Maximum size of the free list

    // Use thread-local counters and aggregate at the end of the run!
    // Use Arrays to store the thread-local counters.
    
    for(int rep = 0; rep < num_repetitions; rep++) 
    {
        printf("Running benchmark repetition %d/%d...\n", rep + 1, num_repetitions);
        int Nr_enq_operations[num_threads];
        int Nr_deq_operations[num_threads];
        int Nr_failed_enq_operations[num_threads];
        int Nr_failed_deq_operations[num_threads];
        int Nr_failed_enq_CAS_operations[num_threads];
        int Nr_failed_deq_CAS_operations[num_threads];
        int Nr_free_list_insertions[num_threads];
        int Max_free_list_size[num_threads];
        double Thread_times[num_threads];
        
        run_benchmark(num_threads, time_interval, enq_specs, deq_specs,
                    Nr_enq_operations, Nr_deq_operations, 
                    Nr_failed_enq_operations, Nr_failed_deq_operations,
                    Nr_failed_enq_CAS_operations, Nr_failed_deq_CAS_operations, 
                    Nr_free_list_insertions, Max_free_list_size, Thread_times);
        //create a results line, that will later be written to a file
        write_results_line(rep, num_threads, time_interval,
                           Nr_enq_operations, Nr_deq_operations,
                           Nr_failed_enq_operations, Nr_failed_deq_operations,
                           Nr_failed_enq_CAS_operations, Nr_failed_deq_CAS_operations,
                           Nr_free_list_insertions, Max_free_list_size, Thread_times, filename);
    }

    // Print results and free memory
    printf("Benchmark completed. Results written to %s\n", filename);

    free(enq_specs);
    free(deq_specs);

    return EXIT_SUCCESS;
}