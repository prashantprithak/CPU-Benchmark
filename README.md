# Parallel / Multi-Threaded QuickSort

## Overview

This project evaluates the performance of **parallel QuickSort** algorithms on a multi-core CPU, comparing two different implementations:

- **Version 1**: Naive parallel implementation with thread creation.
- **Version 2**: Optimized implementation using **Thread Pooling**.

---

## Goals

- Maximize **CPU utilization** across all cores.
- Achieve better performance than sequential QuickSort.
- Minimize thread **creation and synchronization overhead**.
- Compare and conclude the best-performing approach.

---

## Environment

- **Processor**: Intel Core i5-1135G7 @ 2.40GHz (8 logical CPUs)
- **Tools**: Intel VTune for profiling and performance analysis

---

## Strategies

### Version 1: Naive Parallelization

- **Static work distribution** (equal sub-arrays)
- Threads independently sort partitions
- **Problem**: High overhead due to thread creation & context switching
- **Result**: Slower than sequential due to poor scalability and load imbalance

### Version 2: Thread Pooling

- Uses a shared thread pool to manage tasks efficiently
- Reduces thread management overhead
- **Result**: Faster execution and better speedup

---

## Key Findings

- **Thread creation** time (1–10 ms) was larger than the actual sorting time (1–2 ms) for small tasks.
- **Thread management overhead** dominated computation in Version 1.
- **Thread pooling** in Version 2 successfully minimized overhead and improved CPU efficiency.
- **Parallel ≠ faster**, unless overhead is negligible relative to work.

---

## Performance Summary

- **Version 2** outperformed Version 1 and sequential baseline.
- Profiling showed better **thread activity**, reduced idle time, and improved **speedup**.

---

## Conclusion

- Thread overhead can kill performance if not handled wisely.
- **Thread Pooling** is essential for efficient parallel sorting.
- Performance tuning requires balancing task granularity and workload distribution.

---

## Lessons Learned

- Avoid creating more threads than necessary.
- Work must **outweigh** overhead.
- Sometimes, **doing less** (in terms of thread management) achieves **more**.

