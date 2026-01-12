mkdir -p "data/smallbench"

bash -c "./build/benchmark_ex1 1 2 1 1 1 data/smallbench/small_bench_ex1.csv"
bash -c "./build/benchmark_ex2 8 2 1 1 1 data/smallbench/small_bench_ex2.csv"
bash -c "./build/benchmark_ex4 8 2 1 1 1 data/smallbench/small_bench_ex4.csv"
bash -c "./build/benchmark_ex5 8 2 1 1 1 data/smallbench/small_bench_ex5.csv"

bash -c "./build/benchmark_ex1 1 2 5 1 1 data/smallbench/small_bench_ex1.csv"
bash -c "./build/benchmark_ex2 8 2 1 1 1 data/smallbench/small_bench_ex2.csv"
bash -c "./build/benchmark_ex4 8 2 1 1 1 data/smallbench/small_bench_ex4.csv"
bash -c "./build/benchmark_ex5 8 2 1 1 1 data/smallbench/small_bench_ex5.csv"

bash -c "./build/benchmark_ex1 1 2 1 1000 1000 data/smallbench/small_bench_ex1.csv"
bash -c "./build/benchmark_ex2 8 2 1 1000 1000 data/smallbench/small_bench_ex2.csv"
bash -c "./build/benchmark_ex4 8 2 1 1000 1000 data/smallbench/small_bench_ex4.csv"
bash -c "./build/benchmark_ex5 8 2 1 1000 1000 data/smallbench/small_bench_ex5.csv"

bash -c "./build/benchmark_ex1 1 2 5 1000 1000 data/smallbench/small_bench_ex1.csv"
bash -c "./build/benchmark_ex2 8 2 1 1000 1000 data/smallbench/small_bench_ex2.csv"
bash -c "./build/benchmark_ex4 8 2 1 1000 1000 data/smallbench/small_bench_ex4.csv"
bash -c "./build/benchmark_ex5 8 2 1 1000 1000 data/smallbench/small_bench_ex5.csv"

# Change to:
# Variant A only
# Batch size: 1
# Ex 1,2,4,5
# Repetitions: 2
# Threads: 1,2,8,10