CC ?= gcc
RM ?= @rm
MKDIR ?= @mkdir

CFLAGS := -O3 -Wall -Wextra -fopenmp
CHECKCFLAGS := -O3 -Wall -Wextra -fopenmp -DCHECK_CORRECTNESS
CPPFLAGS := -Isrc

SRC_DIR := src
BUILD_DIR := build
DATA_DIR := data
PLOTS_DIR := plots
REPORT_DIR := report

BENCH_VERSIONS := 1 2 4 5
BENCH_BINS := $(foreach v,$(BENCH_VERSIONS),$(BUILD_DIR)/benchmark_ex$(v))
BENCH_OBJ_DIR := $(BUILD_DIR)/bench

all: $(BUILD_DIR) $(DATA_DIR) $(BENCH_BINS)
	@echo "Built benchmarks"

$(DATA_DIR):
	@echo "Creating data directory: $(DATA_DIR)"
	$(MKDIR) $(DATA_DIR)

$(BUILD_DIR):
	@echo "Creating build directory: $(BUILD_DIR)"
	$(MKDIR) $(BUILD_DIR)

$(BENCH_OBJ_DIR):
	@echo "Creating benchmark object directory: $(BENCH_OBJ_DIR)"
	$(MKDIR) $(BENCH_OBJ_DIR)

# Compile one .c -> one .o (Make pattern rule)
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# --- Benchmark executables (bench.c compiled once per VERSION) ---

$(BENCH_OBJ_DIR)/bench_v%.o: $(SRC_DIR)/bench.c | $(BENCH_OBJ_DIR)
	@echo "Compiling $< (VERSION=$*)"
	$(CC) $(CFLAGS) $(CPPFLAGS) -DVERSION=$* -c -o $@ $<

$(BUILD_DIR)/Ex%.o: $(SRC_DIR)/Ex%.c | $(BUILD_DIR)
	@echo "Compiling $<"
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(BUILD_DIR)/benchmark_ex%: $(BENCH_OBJ_DIR)/bench_v%.o $(BUILD_DIR)/Ex%.o | $(BUILD_DIR)
	@echo "Linking $@"
	$(CC) $(CFLAGS) -o $@ $^

bench: $(BENCH_BINS)
	@echo "Running big benchmark via ./bench.sh"
	@bash ./bench.sh

plot:
	@echo "Plotting benchmark results via src/plot.py"
	python3 src/plot.py data/2026-01-13T13:54:12/csv -o plots/benchmark-latest

small-bench: $(BENCH_BINS) $(DATA_DIR)
	@echo "Running small-bench ..."
	@srun -t 1 -p q_student bash ./small_bench.sh

small-bench-check:
	@echo "Cleaning and rebuilding with correctness checks ..."
	$(RM) -Rf $(BUILD_DIR)
	$(MKDIR) $(BUILD_DIR)
	$(MKDIR) $(BENCH_OBJ_DIR)
	$(MAKE) CFLAGS="$(CHECKCFLAGS)" $(BENCH_BINS)
	@echo "Running small-bench with checks ..."
	@srun -t 1 -p q_student bash ./small_bench.sh

requirements:
	@echo "Installing requirements ..."
	@pip3 install -r src/requirements.txt

small-plot:
	@echo "Plotting small-bench results ..."
	@echo "Python Libraries used: matplotlib, pandas, numpy"
	python3 src/plot.py data/smallbench/csv -o plots/smallbench

report: small-plot
	@echo "Compiling report ..."
	pdflatex -output-directory=$(REPORT_DIR) report/smallbench-plots.tex

zip:
	@zip project.zip bench.sh small_bench.sh Makefile src/* run_nebula.sh

zip-full:
	@zip project.zip bench.sh small_bench.sh Makefile src/* plots/benchmark-latest/* report/* run_nebula.sh CMakeLists.txt

clean:
	@echo "Cleaning build directory: $(BUILD_DIR) and benchmark binaries"
	$(RM) -Rf $(BUILD_DIR)

.PHONY: all clean report bench small-bench small-plot zip zip-full requirements plot small-bench-check
