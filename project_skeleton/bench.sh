#!/usr/bin/env bash
set -euo pipefail

# Big benchmark runner (simple + readable).
#
# Bench binary interface:
#   ./build/benchmark_ex<VER> <threads> <repetitions> <seconds> <enq_pattern> <deq_pattern> [output_csv]
#
# Required experiments:
# - Ex1 (sequential): threads=1, seconds in {1,5}, batch in {1,1000}, repetitions=10.
# - Ex2/Ex4/Ex5 (concurrent): p in {1,2,8,10,20,32,45,64}, batch in {1,1000}, 4 configs, repetitions=10.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

TS="$(date +%Y-%m-%dT%H:%M:%S)"
OUTDIR="data/${TS}/csv"
mkdir -p "$OUTDIR"

echo "Benchmark run: ${TS}"
echo "Output dir: ${OUTDIR}"

die() {
  echo "Error: $*" >&2
  exit 1
}

run_one() {
  local v="$1" p="$2" reps="$3" secs="$4" enq="$5" deq="$6" out="$7"
  local bin="./build/benchmark_ex${v}"
  [[ -x "$bin" ]] || die "Missing binary: $bin (run: make all)"
  echo "ex${v} p=${p} s=${secs} r=${reps} enq=${enq} deq=${deq} -> ${out}"
  srun -t 1 -p q_student bash -c 'LD_PRELOAD="$HOME/mimalloc/out/release/libmimalloc.so" exec "$0" "$@"' "$bin" "$p" "$reps" "$secs" "$enq" "$deq" "$out"
}

# Build "{a,b,c,...}" list strings for per-thread batch sizes.
mk_list() {
  local -a vals=("$@")
  local out="{" i
  for ((i = 0; i < ${#vals[@]}; i++)); do
    out+="${vals[i]}"
    if ((i + 1 < ${#vals[@]})); then
      out+=","
    fi
  done
  out+="}"
  echo "$out"
}

mk_lists_one_enq_rest_deq() {
  local p="$1" b="$2" i
  local -a enq_vals=() deq_vals=()
  for ((i = 0; i < p; i++)); do
    if ((i == 0)); then
      enq_vals+=("$b")
      deq_vals+=("0")
    else
      enq_vals+=("0")
      deq_vals+=("$b")
    fi
  done
  echo "$(mk_list "${enq_vals[@]}")|$(mk_list "${deq_vals[@]}")"
}

mk_lists_half_enq_half_deq() {
  local p="$1" b="$2" i
  local -a enq_vals=() deq_vals=()
  for ((i = 0; i < p; i++)); do
    if ((i < p / 2)); then
      enq_vals+=("$b")
      deq_vals+=("0")
    else
      enq_vals+=("0")
      deq_vals+=("$b")
    fi
  done
  echo "$(mk_list "${enq_vals[@]}")|$(mk_list "${deq_vals[@]}")"
}

mk_lists_even_enq_odd_deq() {
  local p="$1" b="$2" i
  local -a enq_vals=() deq_vals=()
  for ((i = 0; i < p; i++)); do
    if ((i % 2 == 0)); then
      enq_vals+=("$b")
      deq_vals+=("0")
    else
      enq_vals+=("0")
      deq_vals+=("$b")
    fi
  done
  echo "$(mk_list "${enq_vals[@]}")|$(mk_list "${deq_vals[@]}")"
}

# -------------------- Ex1 (sequential) --------------------
SEQ_SECONDS=(1 5)
SEQ_BATCHES=(1 1000)
SEQ_REPS=10

echo
echo "== Ex1 (sequential queue, p=1) =="
for secs in "${SEQ_SECONDS[@]}"; do
  for b in "${SEQ_BATCHES[@]}"; do
    out="${OUTDIR}/ex1_seq_p1_b${b}_s${secs}_r${SEQ_REPS}.csv"
    run_one 1 1 "$SEQ_REPS" "$secs" "$b" "$b" "$out"
  done
done

# ---------------- Ex2/Ex4/Ex5 (concurrent) ----------------
CONC_VERSIONS=(2 4 5)
THREAD_COUNTS=(1 2 8 10 20 32 45 64)
CONC_BATCHES=(1 1000)
CONC_SECONDS=1
CONC_REPS=10

echo
echo "== Ex2/Ex4/Ex5 (concurrent queues) =="

for v in "${CONC_VERSIONS[@]}"; do
  echo
  echo "-- ex${v} --"
  for p in "${THREAD_COUNTS[@]}"; do
    for b in "${CONC_BATCHES[@]}"; do
      # a) all threads enq+deq with same batch sizes
      out_a="${OUTDIR}/ex${v}_a_p${p}_b${b}_s${CONC_SECONDS}_r${CONC_REPS}.csv"
      run_one "$v" "$p" "$CONC_REPS" "$CONC_SECONDS" "$b" "$b" "$out_a"

      # b) one thread enq, all other threads deq
      IFS='|' read -r enq_b deq_b <<<"$(mk_lists_one_enq_rest_deq "$p" "$b")"
      out_b="${OUTDIR}/ex${v}_b_p${p}_b${b}_s${CONC_SECONDS}_r${CONC_REPS}.csv"
      run_one "$v" "$p" "$CONC_REPS" "$CONC_SECONDS" "$enq_b" "$deq_b" "$out_b"

      # c) first half enq only, second half deq only (other side gets batch size 0)
      IFS='|' read -r enq_c deq_c <<<"$(mk_lists_half_enq_half_deq "$p" "$b")"
      out_c="${OUTDIR}/ex${v}_c_p${p}_b${b}_s${CONC_SECONDS}_r${CONC_REPS}.csv"
      run_one "$v" "$p" "$CONC_REPS" "$CONC_SECONDS" "$enq_c" "$deq_c" "$out_c"

      # d) even threads enq only, odd threads deq only
      IFS='|' read -r enq_d deq_d <<<"$(mk_lists_even_enq_odd_deq "$p" "$b")"
      out_d="${OUTDIR}/ex${v}_d_p${p}_b${b}_s${CONC_SECONDS}_r${CONC_REPS}.csv"
      run_one "$v" "$p" "$CONC_REPS" "$CONC_SECONDS" "$enq_d" "$deq_d" "$out_d"
    done
  done
done

echo
echo "Done. Results in ${OUTDIR}"
