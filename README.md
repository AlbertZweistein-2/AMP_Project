# AMP Project

Implementation and evaluation of several **queue-based concurrent data structures** in C as part of the TU Wien course **Advanced Multiprocessor Programming (AMP)**. The project compares a sequential queue with three concurrent variants and studies their behavior under different workloads, contention patterns, and thread counts.

## Overview

This project focuses on designing, implementing, and benchmarking the following queue variants:

- **Sequential queue**
- **Concurrent queue with a global lock**
- **Concurrent queue with separate enqueue/dequeue locks**
- **Lock-free concurrent queue using CAS**
- additionally, a **concurrent bag** design is discussed conceptually

The main goal was to compare these implementations with respect to:

- throughput
- scalability
- fairness across threads
- failed operations / contention
- memory reuse behavior via thread-local free lists

## Implemented Data Structures

### 1. Sequential Queue

A sequential unbounded FIFO queue implemented in C using a singly linked list with a sentinel node. Nodes are recycled through **thread-local free lists** to reduce allocation overhead. Although free lists are maintained per thread ID, this version is intended for sequential use only and is **not thread-safe**.

**Highlights**
- singly linked list
- sentinel node
- node reuse via local free lists
- baseline for correctness and performance

### 2. Concurrent Queue with Global Lock

This version extends the sequential queue with a single **OpenMP lock** protecting the whole data structure. The design is simple and linearizable, but all queue operations are fully serialized.

**Highlights**
- one global lock for `enq` and `deq`
- easy correctness argument
- serves as concurrent baseline
- suffers from strong contention under load

### 3. Concurrent Queue with Separate Locks

This queue uses **two OpenMP locks**: one for enqueue operations and one for dequeue operations. This reduces contention and allows one thread to enqueue while another dequeues concurrently. A special edge case arises when the last element is removed, requiring careful synchronization to keep `head` and `tail` consistent.

**Highlights**
- separate `enq_lock` and `deq_lock`
- higher throughput than global locking in most scenarios
- explicitly handles the “last element removed” edge case
- linearizability discussed in the report and pseudocode appendix

### 4. Lock-Free Concurrent Queue

A lock-free queue based on the Michael-Scott style approach using **C11 atomics** and **Compare-And-Swap (CAS)** for head/tail updates. To avoid ABA-style problems, the implementation uses **Quiescent State Based Reclamation (QSBR)** for deferred memory reclamation.

**Highlights**
- lock-free progress via CAS
- head/tail as atomic pointers
- QSBR-based memory reclamation
- strong fairness properties under imbalanced workloads

**Implementation Mistake**
- If a thread dies, the QSBR-based memory reclamation using while loops to wait for quiescence can lead to deadlock if a thread dies while another thread is waiting for it to reach a quiescent state. This could be mitigated by using for loops. This makes the queue actually not lock-free in the presence of thread failures, as a failed thread can block progress indefinitely.

### 5. Concurrent Bag (Concept)

The report also discusses a bag built from multiple lock-free queues, with round-robin enqueue/dequeue behavior. This design relaxes FIFO ordering and improves distribution, but the simple scanning approach discussed in the report is **not linearizable** for failed dequeue operations.

## Benchmark Design

A configurable benchmark was built to compare all queue implementations fairly. Thread local counters were used to prevent false sharing and to track per-thread statistics. The benchmark supports various workload patterns, including balanced and imbalanced enqueue/dequeue ratios, as well as different batch sizes.
The implementation variant is selected at compile time via preprocessor flags:

- `-DVERSION=1` → sequential queue
- `-DVERSION=2` → global-lock queue
- `-DVERSION=4` → two-lock queue
- `-DVERSION=5` → lock-free queue

Additional flags:
- `-DDEBUG`
- `-DCHECK_CORRECTNESS`

The benchmark supports:
- configurable thread count
- configurable repetitions
- fixed runtime per experiment
- flexible enqueue/dequeue batch-size patterns
- CSV export of per-thread statistics

Recorded metrics include:
- successful enqueue/dequeue operations
- failed operations
- failed CAS operations for the lock-free queue
- free-list insertions
- maximum free-list size
- actual runtime

## Experimental Setup

Benchmarks were run on a **nebula** cluster node with:

- **2 sockets**
- **32 cores per socket**
- **64 total physical cores**
- **AMD EPYC 7551**
- **256 GiB RAM**
- **8 NUMA nodes**

The sequential queue was benchmarked separately with single-threaded runs, while the concurrent variants were evaluated across multiple workload variants, batch sizes, and thread counts.

## Results Summary

The benchmark results show a clear overall pattern:

- the **sequential queue** achieved the highest throughput
- none of the concurrent queues outperformed the sequential baseline
- the **two-lock queue** delivered the best throughput among the concurrent implementations in most scenarios
- the **lock-free queue** showed the best fairness and the most balanced thread participation
- the **global-lock queue** performed worst under contention and imbalanced workloads

### Main observations

**Throughput**
- All concurrent variants incur synchronization overhead and are slower than the sequential baseline.
- The two-lock queue generally performs best among the concurrent designs.

**Fairness**
- The lock-free queue distributes work most evenly across threads, especially in imbalanced workloads. The thread-activity plot on page 18 shows the flattest curves for the lock-free variant in several workload variants, indicating the most even work distribution.

**CAS contention**
- The lock-free queue pays for fairness with higher CAS contention. The stacked plots on page 14 show failed CAS attempts rising with thread count, especially under imbalanced and highly concurrent workloads.

**Memory footprint**
- The lock-free queue consistently keeps smaller free lists. The plots on page 16 show noticeably lower maximum thread-local free-list sizes for the lock-free implementation, often much smaller than the lock-based variants.

## Key Takeaway

This project shows that **concurrent data structures are not automatically faster** than well-optimized sequential ones. In practice, synchronization costs, cache-coherence traffic, CAS retries, and memory-management overhead can dominate performance. The best implementation depends on the goal:

- **Global lock**: simplest, correctness baseline
- **Two locks**: best throughput in most concurrent scenarios
- **Lock-free**: best fairness and progress distribution
- **Sequential**: fastest overall when concurrency is not required
