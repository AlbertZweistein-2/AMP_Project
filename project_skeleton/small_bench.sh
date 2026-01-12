#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

OUTDIR="data/smallbench/csv"
mkdir -p "$OUTDIR"

die() {
	echo "Error: $*" >&2
	exit 1
}

run_one() {
	local v="$1" p="$2" reps="$3" secs="$4" enq="$5" deq="$6" out="$7"
	local bin="./build/benchmark_ex${v}"
	[[ -x "$bin" ]] || die "Missing binary: $bin (run: make all)"
	echo "ex${v} p=${p} s=${secs} r=${reps} enq=${enq} deq=${deq} -> ${out}"
	"$bin" "$p" "$reps" "$secs" "$enq" "$deq" "$out"
}

THREAD_COUNTS=(1 2 8 10 20 32 45 64)
REPS=2
BENCH_SECONDS=1
BATCH=1

echo "Small benchmark output dir: ${OUTDIR}"

echo
echo "== Ex1 (sequential queue) =="
run_one 1 1 "$REPS" "$BENCH_SECONDS" "$BATCH" "$BATCH" "${OUTDIR}/ex1_seq_p1_b${BATCH}_s${BENCH_SECONDS}_r${REPS}.csv"

echo
echo "== Ex2/Ex4/Ex5 (Variant A only) =="
for v in 2 4 5; do
	echo
	echo "-- ex${v} --"
	for p in "${THREAD_COUNTS[@]}"; do
		run_one "$v" "$p" "$REPS" "$BENCH_SECONDS" "$BATCH" "$BATCH" "${OUTDIR}/ex${v}_a_p${p}_b${BATCH}_s${BENCH_SECONDS}_r${REPS}.csv"
	done
done

echo
echo "Done. Results in ${OUTDIR}"