#!/usr/bin/env python3
import sys
import argparse
import re
from pathlib import Path

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

plt.rcParams.update({
    "figure.dpi": 120,
    "savefig.dpi": 300,
    "axes.grid": True,
    "grid.alpha": 0.25,
})

FILENAME_RE = re.compile(
    r"^ex(?P<exercise>\d+)_(?P<variant>[a-z]+)_p(?P<p>\d+)_b(?P<b>\d+)_s(?P<s>\d+)_r(?P<r>\d+)\.csv$"
    r"|^ex(?P<exercise2>\d+)_seq_p(?P<p2>\d+)_b(?P<b2>\d+)_s(?P<s2>\d+)_r(?P<r2>\d+)\.csv$"
    r"$",
    flags=re.IGNORECASE,
 )

def _preferred_values(present: list[int], preferred: list[int]) -> list[int]:
    # Keep a stable ordering (e.g. [1, 1000]) but only include existing values.
    present_set = set(present)
    out = [v for v in preferred if v in present_set]
    return out or sorted(present)

def _as_list(x):
    # Normalize matplotlib return types (single item vs list/ndarray).
    if isinstance(x, (list, tuple, np.ndarray)):
        return list(x)
    return [x]

def _safe_legend(ax, **kwargs):
    # Add a legend only if there are labeled artists.
    handles, labels = ax.get_legend_handles_labels()
    filtered = [(h, l) for h, l in zip(handles, labels) if l and not l.startswith("_")]
    if not filtered:
        return
    h2, l2 = zip(*filtered)
    ax.legend(h2, l2, **kwargs)

def _no_data(ax, text: str = "no data"):
    # Mark an axis as intentionally empty.
    ax.text(0.5, 0.5, text, transform=ax.transAxes, ha="center", va="center", color="0.5", fontsize=9)

DEFAULT_BATCHES = [1, 1000]
DEFAULT_VARIANTS = ["a", "b", "c", "d"]
DEFAULT_THREAD_XLIM = (0, 66)
# DEFAULT_THREAD_TICKS = [1, 2, 8, 10, 20, 32, 45, 66]

def _set_thread_axis(ax):
    # Keep the thread x-axis consistent across all plots.
    ax.set_xlim(*DEFAULT_THREAD_XLIM)
    # ax.set_xticks(DEFAULT_THREAD_TICKS)

def parse_filename(path: Path) -> dict:
    # Parse benchmark metadata from the CSV filename.
    name = path.name
    m = FILENAME_RE.match(name)
    if not m:
        raise ValueError(f"Unrecognized filename format: {name}")
    g = m.groupdict()
    if g.get("exercise") is not None:
        exercise = int(g["exercise"])
        variant = g["variant"].lower()
        p = int(g["p"])
        b = int(g["b"])
        s = int(g["s"])
        r = int(g["r"])
        mode = "concurrent"
    else:
        exercise = int(g["exercise2"])
        variant = "seq"
        p = int(g["p2"])
        b = int(g["b2"])
        s = int(g["s2"])
        r = int(g["r2"])
        mode = "sequential"
    return {
        "file": name,
        "exercise": exercise,
        "variant": variant,   # a/b/c/d or seq
        "p_from_name": p,
        "batch": b,
        "s": s,
        "r_from_name": r,
        "mode": mode,
    }

def infer_impl(exercise: int, variant: str) -> str:
    # Map (exercise, variant) to a short label used in plots.
    if exercise == 1 and variant == "seq":
        return "seq"
    if exercise == 4:
        return "ex4"
    if exercise == 5:
        return "ex5"
    if exercise == 2:
        return "ex2"
    return f"ex{exercise}_{variant}"

def load_one_csv(path: Path) -> pd.DataFrame:
    # Load one CSV and attach filename-derived metadata columns.
    meta = parse_filename(path)
    df = pd.read_csv(path)

    expected_cols = [
        "Repetition","NumThreads","TimeInterval","ActualBenchTime","ThreadID",
        "NrEnqOps","NrDeqOps",
        "NrFailedEnqOps","NrFailedDeqOps",
        "NrFailedEnqCASOps","NrFailedDeqCASOps",
        "NrFreeListInsertions","MaxFreeListSize",
    ]
    for c in expected_cols:
        if c in df.columns:
            df[c] = pd.to_numeric(df[c], errors="coerce")

    df["exercise"] = meta["exercise"]
    df["variant"] = meta["variant"]
    df["impl"] = infer_impl(meta["exercise"], meta["variant"])
    df["batch"] = meta["batch"]
    df["s"] = meta["s"]
    df["r_from_name"] = meta["r_from_name"]
    df["file"] = meta["file"]
    return df

def load_all_csvs(folder: Path) -> pd.DataFrame:
    # Load and concatenate all CSVs in a folder.
    paths = sorted(folder.glob("*.csv"))
    if not paths:
        raise FileNotFoundError(f"No CSVs found under: {folder}")
    parts = [load_one_csv(p) for p in paths]
    return pd.concat(parts, ignore_index=True)

def per_run_metrics(df: pd.DataFrame) -> pd.DataFrame:
    # Aggregate per-thread rows into per-run (per repetition) metrics.
    group_cols = ["file","exercise","variant","impl","batch","s","Repetition","NumThreads"]
    available_group_cols = [c for c in group_cols if c in df.columns]
    
    g = df.groupby(available_group_cols, dropna=False)

    agg_dict = {}
    if "TimeInterval" in df.columns: agg_dict["TimeInterval_s"] = ("TimeInterval", "first")
    if "ActualBenchTime" in df.columns: agg_dict["ActualBenchTime_s"] = ("ActualBenchTime", "mean")
    if "NrEnqOps" in df.columns: agg_dict["total_enq"] = ("NrEnqOps", "sum")
    if "NrDeqOps" in df.columns: agg_dict["total_deq"] = ("NrDeqOps", "sum")
    if "NrFailedEnqOps" in df.columns: agg_dict["total_failed_enq"] = ("NrFailedEnqOps", "sum")
    if "NrFailedDeqOps" in df.columns: agg_dict["total_failed_deq"] = ("NrFailedDeqOps", "sum")
    if "NrFailedEnqCASOps" in df.columns: agg_dict["total_failed_enq_cas"] = ("NrFailedEnqCASOps", "sum")
    if "NrFailedDeqCASOps" in df.columns: agg_dict["total_failed_deq_cas"] = ("NrFailedDeqCASOps", "sum")
    if "NrFreeListInsertions" in df.columns: agg_dict["freelist_insertions"] = ("NrFreeListInsertions", "sum")
    if "MaxFreeListSize" in df.columns: agg_dict["max_freelist_size_global"] = ("MaxFreeListSize", "max")

    out = g.agg(**agg_dict).reset_index()

    if "total_enq" in out.columns and "total_deq" in out.columns:
        out["total_ops"] = out["total_enq"] + out["total_deq"]
        if "ActualBenchTime_s" in out.columns:
            out["throughput_ops_s"] = out["total_ops"] / out["ActualBenchTime_s"]
            if "freelist_insertions" in out.columns:
                 out["freelist_inserts_s"] = out["freelist_insertions"] / out["ActualBenchTime_s"]

    if "total_ops" in out.columns:
        denom = out["total_ops"].replace(0, pd.NA)
        if "total_failed_enq_cas" in out.columns and "total_failed_deq_cas" in out.columns:
            out["failed_cas_per_op"] = (out["total_failed_enq_cas"] + out["total_failed_deq_cas"]) / denom
            out["failed_enq_cas_per_op"] = out["total_failed_enq_cas"] / denom
            out["failed_deq_cas_per_op"] = out["total_failed_deq_cas"] / denom

    return out

def plot_seq_throughput(seq_df: pd.DataFrame, out_path: Path):
    # Plot sequential throughput as bars per batch size and duration.
    batches = sorted(seq_df['batch'].unique())
    durations = sorted(seq_df['s'].unique())
    width = 0.35

    fig, ax = plt.subplots(figsize=(6, 4))
    for i, s_val in enumerate(durations):
        means = []
        stds = []
        for b in batches:
            vals = seq_df[(seq_df['batch'] == b) & (seq_df['s'] == s_val)]['throughput_ops_s']
            means.append(vals.mean())
            stds.append(vals.std())
        x = [j + i * width for j in range(len(batches))]
        ax.bar(x, means, width=width, yerr=stds, label=f"{s_val}s", alpha=0.8)

    ax.set_title("Sequential Implementation Throughput")
    ax.set_xlabel("Batch Size")
    ax.set_ylabel("Throughput (ops/s)")
    ax.set_xticks([r + width / 2 for r in range(len(batches))])
    ax.set_xticklabels(batches)
    ax.legend(title="Run Duration (s)")
    plt.tight_layout()
    plt.savefig(out_path)
    plt.close(fig)

def plot_throughput(run_df: pd.DataFrame, out_path: Path):
    # Plot throughput vs threads for each variant and batch size.
    filtered_df = run_df[run_df["exercise"] != 1].copy()
    if filtered_df.empty: return
    
    agg_df = filtered_df.groupby(
        ["variant", "batch", "exercise", "NumThreads"], as_index=False
    )["throughput_ops_s"].mean()

    ex1_df = run_df[(run_df["exercise"] == 1) & (run_df["TimeInterval_s"] == 1)].copy()
    ref_agg = ex1_df.groupby(["batch", "TimeInterval_s"], as_index=False)["throughput_ops_s"].mean()

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    legend_map = {2: "Global Lock (Ex 2)", 4: "Two Locks (Ex 4)", 5: "Lock-Free (Ex 5)"}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        
        for ax, batch in zip(axs, batches):
            ref_data = ref_agg[ref_agg['batch'] == batch]
            for _, row in ref_data.iterrows():
                val = row['throughput_ops_s']
                ax.axhline(y=val, color='red', linestyle='--', alpha=0.7, label='Reference (Ex 1)')
            
            data = agg_df[(agg_df['variant'] == variant) & (agg_df['batch'] == batch)]
            if data.empty:
                _no_data(ax)
            for exercise in sorted(data['exercise'].unique()):
                ex_data = data[data['exercise'] == exercise]
                ax.plot(ex_data['NumThreads'], ex_data['throughput_ops_s'], 
                        marker='o', label=legend_map.get(exercise, f'Ex {exercise}'))

            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_yscale("log")
            ax.grid(True, which="both", ls="-", alpha=0.3)
            ax.set_xlabel("Number of Threads")
            _set_thread_axis(ax)
            ax.set_ylim(4e4, 4e8) 
            if batch == 1:
                ax.set_ylabel("Average Throughput (ops/s)")
            _safe_legend(ax, title="Exercise", loc='upper right', fontsize='x-small')
    
    plt.savefig(out_path)
    plt.close(fig)

def plot_speedup(run_df: pd.DataFrame, out_path: Path):
    # Plot speedup (throughput / sequential baseline) vs threads.
    seq_baseline = run_df[
        (run_df["exercise"] == 1) & 
        (run_df["NumThreads"] == 1)
    ].groupby("batch")["throughput_ops_s"].mean()

    filtered_df = run_df[run_df["exercise"] != 1].copy()
    if filtered_df.empty: return

    agg_df = filtered_df.groupby(
        ["variant", "batch", "exercise", "NumThreads"], as_index=False
    )["throughput_ops_s"].mean()

    def calculate_speedup(row):
        baseline = seq_baseline.get(row['batch'], np.nan)
        if pd.isna(baseline) or baseline == 0:
            return np.nan
        return row['throughput_ops_s'] / baseline

    agg_df['speedup'] = agg_df.apply(calculate_speedup, axis=1)

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    legend_map = {2: "Global Lock (Ex 2)", 4: "Two Locks (Ex 4)", 5: "Lock-Free (Ex 5)"}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        for ax, batch in zip(axs, batches):
            ax.axhline(y=1.0, color='red', linestyle='--', alpha=0.7, label='Reference (Ex 1)')
            data = agg_df[(agg_df['variant'] == variant) & (agg_df['batch'] == batch)]
            if data.empty:
                _no_data(ax)
            for exercise in sorted(data['exercise'].unique()):
                ex_data = data[data['exercise'] == exercise]
                ax.plot(ex_data['NumThreads'], ex_data['speedup'], marker='o', label=legend_map.get(exercise, f'Ex {exercise}'))
            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_yscale("log")
            ax.grid(True, which="both", ls="-", alpha=0.3)
            ax.set_xlabel("Number of Threads")
            _set_thread_axis(ax)
            ax.set_ylim(2e-5, 2) 
            if batch == 1:
                ax.set_ylabel("Slowdown (vs Sequential)")
            _safe_legend(ax, title="Exercise", loc='upper right', fontsize='x-small')

    plt.savefig(out_path)
    plt.close(fig)

def plot_contention(run_df: pd.DataFrame, out_path: Path):
    # Plot failed CAS per successful op for exercise 5.
    ex5_df = run_df[run_df["exercise"] == 5].copy()
    if ex5_df.empty: return

    ex5_df['failed_cas_sum'] = ex5_df['total_failed_enq_cas'] + ex5_df['total_failed_deq_cas']
    ex5_df['cas_failure_ratio'] = ex5_df['failed_cas_sum'] / ex5_df['total_ops']
    agg_df = ex5_df.groupby(["variant", "batch", "NumThreads"], as_index=False)["cas_failure_ratio"].mean()

    batches = list(DEFAULT_BATCHES)
    fig, axs = plt.subplots(1, 2, figsize=(12, 6), constrained_layout=True, sharey=True)
    fig.suptitle("Contention: Failed CAS Operations per Successful Op (Exercise 5)", fontsize=14, fontweight='bold')
    axs = _as_list(axs)
    variants = sorted(agg_df['variant'].unique())
    markers = ['o', 's', 'D', '^']

    for ax, batch in zip(axs, batches):
        batch_data = agg_df[agg_df['batch'] == batch]
        if batch_data.empty:
            _no_data(ax)
        for i, variant in enumerate(variants):
            data = batch_data[batch_data['variant'] == variant]
            if data.empty: continue
            ax.plot(data['NumThreads'], data['cas_failure_ratio'], label=f"Variant {variant.upper()}",
                    marker=markers[i % len(markers)], linewidth=2, markersize=6)
        ax.set_title(f"Batch Size = {batch}", fontsize=12)
        ax.set_yscale("linear") 
        ax.grid(True, which="major", ls="-", alpha=0.3)
        ax.set_xlabel("Number of Threads")
        _set_thread_axis(ax)
        if batch == 1:
            ax.set_ylabel("Failed CAS per Operation (Average)")
        _safe_legend(ax, title="Variant", fontsize='small')

    plt.savefig(out_path)
    plt.close(fig)

def plot_failed_cas_stack(run_df: pd.DataFrame, out_path: Path):
    # Stackplot of enqueue/dequeue CAS failure ratios (exercise 5).
    ex5_df = run_df[run_df["exercise"] == 5].copy()
    if ex5_df.empty: return
    
    ex5_df['enq_fail_ratio'] = ex5_df['total_failed_enq_cas'] / ex5_df['total_enq']
    ex5_df['deq_fail_ratio'] = ex5_df['total_failed_deq_cas'] / ex5_df['total_deq']
    agg_df = ex5_df.groupby(["variant", "batch", "NumThreads"], as_index=False)[['enq_fail_ratio', 'deq_fail_ratio']].mean()
    agg_df.sort_values(by="NumThreads", inplace=True)

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    labels = ['Failed Enq CAS', 'Failed Deq CAS']

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        for ax, batch in zip(axs, batches):
            data = agg_df[(agg_df['variant'] == variant) & (agg_df['batch'] == batch)]
            if not data.empty:
                x = data['NumThreads']
                y1 = data['enq_fail_ratio']
                y2 = data['deq_fail_ratio']
                ax.stackplot(x, y1, y2, labels=labels, alpha=0.8)
            else:
                _no_data(ax)
            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_yscale("linear") 
            ax.grid(True, which="major", ls="-", alpha=0.3)
            ax.set_xlabel("Number of Threads")
            # _set_thread_axis(ax)
            ax.set_xlim(1, 64)
            ax.margins(x=0)
            ax.set_ylim(0, 11)
            if batch == 1:
                ax.set_ylabel("Failed CAS per Operation (Average)")
            _safe_legend(ax, loc='upper left', fontsize='small')

    plt.savefig(out_path)
    plt.close(fig)

def plot_failed_ops_s(run_df: pd.DataFrame, out_path: Path):
    # Plot failed enqueue/dequeue operations per second.
    run_df['failed_enq_s'] = run_df['total_failed_enq'] / run_df['ActualBenchTime_s']
    run_df['failed_deq_s'] = run_df['total_failed_deq'] / run_df['ActualBenchTime_s']
    
    filtered_df = run_df[run_df["exercise"] != 1].copy()
    if filtered_df.empty: return
    
    agg_df = filtered_df.groupby(
        ["variant", "batch", "exercise", "NumThreads"], as_index=False
    )[['failed_enq_s', 'failed_deq_s']].mean()

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    color_map = {2: 'tab:blue', 4: 'tab:orange', 5: 'tab:green'}
    name_map = {2: "Global Lock (Ex 2)", 4: "Two Locks (Ex 4)", 5: "Lock-Free (Ex 5)"}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        for ax, batch in zip(axs, batches):
            data = agg_df[(agg_df['variant'] == variant) & (agg_df['batch'] == batch)]
            if data.empty:
                _no_data(ax)
            for exercise in sorted(data['exercise'].unique()):
                ex_data = data[data['exercise'] == exercise]
                c = color_map.get(exercise, 'black')
                if ex_data['failed_enq_s'].sum() > 0:
                    ax.plot(ex_data['NumThreads'], ex_data['failed_enq_s'], marker='o', linestyle='-', color=c, label='_nolegend_')
                if ex_data['failed_deq_s'].sum() > 0:
                    ax.plot(ex_data['NumThreads'], ex_data['failed_deq_s'], marker='x', linestyle='--', color=c, label='_nolegend_')

            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_yscale("log")
            ax.grid(True, which="both", ls="-", alpha=0.3)
            ax.set_xlabel("Number of Threads")
            _set_thread_axis(ax)
            ax.set_ylim(1e-2, 1e9) 
            if batch == 1:
                ax.set_ylabel("Failed Operations per Second")
            if batch == 1000:
                legend_elements = [Line2D([0], [0], color=color_map[ex], lw=2, label=name_map[ex]) 
                                   for ex in sorted(color_map.keys()) if ex in data['exercise'].unique()]
                legend_elements.append(Line2D([0], [0], color='black', lw=0, label='')) 
                legend_elements.append(Line2D([0], [0], color='gray', lw=2, linestyle='-', marker='o', label='Failed Enqueue'))
                legend_elements.append(Line2D([0], [0], color='gray', lw=2, linestyle='--', marker='x', label='Failed Dequeue'))
                if legend_elements:
                    ax.legend(handles=legend_elements, loc='upper left', fontsize='x-small', framealpha=0.9)

    plt.savefig(out_path)
    plt.close(fig)

def plot_balance_scatter(raw: pd.DataFrame, out_path: Path):
    # Scatter plot of enqueue vs dequeue ops (+1) on log scales.
    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    
    valid_raw = raw[raw['exercise'] != 1]
    if valid_raw.empty: return
    
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    color_map = {1: 'tab:red', 2: 'tab:blue', 4: 'tab:orange', 5: 'tab:green'}
    legend_map = {2: "Ex 2", 4: "Ex 4", 5: "Ex 5"}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True, sharex=True))
        for ax, batch in zip(axs, batches):
            data = valid_raw[(valid_raw['variant'] == variant) & (valid_raw['batch'] == batch)]
            if data.empty:
                _no_data(ax)
            for exercise in sorted(data['exercise'].unique()):
                ex_data = data[data['exercise'] == exercise]
                ax.scatter(ex_data['NrEnqOps'] + 1, ex_data['NrDeqOps'] + 1, 
                           color=color_map.get(exercise, 'gray'),
                           alpha=0.4, edgecolors='none', s=25, 
                           label=legend_map.get(exercise, str(exercise)))
            ax.plot([1e0, 1e8], [1e0, 1e8], 'k--', alpha=0.3, zorder=0, label='Perfect Balance')
            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_xlabel("Number of Enqueue Operations (+1)")
            ax.set_xscale("log")
            ax.set_yscale("log")
            ax.set_xlim(left=0.8)
            ax.set_ylim(bottom=0.8)
            ax.grid(True, which="both", ls="-", alpha=0.2)
            if batch == 1:
                ax.set_ylabel("Number of Dequeue Operations (+1)")
            handles, labels = ax.get_legend_handles_labels()
            by_label = {l: h for h, l in zip(handles, labels) if l and not l.startswith("_")}
            if by_label:
                ax.legend(by_label.values(), by_label.keys(), title="Exercise", loc='upper right', fontsize='x-small')

    plt.savefig(out_path)
    plt.close(fig)

def plot_thread_imbalance(raw: pd.DataFrame, out_path: Path):
    # Plot sorted per-thread work profiles at the maximum thread count.
    max_threads = raw['NumThreads'].max()
    if pd.isna(max_threads): return

    subset_df = raw[(raw['NumThreads'] == max_threads) & (raw['exercise'] != 1)].copy()
    if subset_df.empty: return
    subset_df['total_ops'] = subset_df['NrEnqOps'] + subset_df['NrDeqOps']

    def get_ranked_profile(df, variant, batch, exercise):
        config_data = df[(df['variant'] == variant) & (df['batch'] == batch) & (df['exercise'] == exercise)]
        if config_data.empty: return None, None
        profiles = []
        for rep in config_data['Repetition'].unique():
            rep_ops = config_data[config_data['Repetition'] == rep]['total_ops'].values
            rep_ops_sorted = np.sort(rep_ops)[::-1]
            if len(rep_ops_sorted) == max_threads:
                profiles.append(rep_ops_sorted)
        if not profiles: return None, None
        profiles_arr = np.array(profiles)
        return np.mean(profiles_arr, axis=0), np.std(profiles_arr, axis=0)

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()
    
    rank_axis = np.arange(1, max_threads + 1)
    legend_map = {2: "Ex 2 (Global)", 4: "Ex 4 (2 Locks)", 5: "Ex 5 (Lock-Free)"}
    color_map = {2: 'tab:blue', 4: 'tab:orange', 5: 'tab:green'}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        for ax, batch in zip(axs, batches):
            any_plotted = False
            for exercise in [2, 4, 5]:
                mean_prof, std_prof = get_ranked_profile(subset_df, variant, batch, exercise)
                if mean_prof is not None:
                    any_plotted = True
                    ax.plot(rank_axis, mean_prof, color=color_map.get(exercise, 'black'), lw=2, label=legend_map.get(exercise, str(exercise)))
                    ax.fill_between(rank_axis, mean_prof - std_prof, mean_prof + std_prof, alpha=0.2)
            if not any_plotted:
                _no_data(ax)
            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_xlabel("Threads (Sorted by Activity)")
            ax.set_yscale("log") 
            ax.grid(True, which="both", ls="-", alpha=0.2)
            ax.set_xlim(1, 64)
            # ax.set_xticks(DEFAULT_THREAD_TICKS)
            if batch == 1:
                ax.set_ylabel(f"Total Operations (at {max_threads} Threads)")
            _safe_legend(ax, fontsize='x-small')

    plt.savefig(out_path)
    plt.close(fig)

def plot_freelist_size(run_df: pd.DataFrame, out_path: Path):
    # Plot mean maximum free-list size vs threads.
    filtered_df = run_df[run_df["exercise"] != 1].copy()
    if filtered_df.empty: return
    if "max_freelist_size_global" not in filtered_df.columns: return
    
    agg_df = filtered_df.groupby(
        ["variant", "batch", "exercise", "NumThreads"], as_index=False
    )["max_freelist_size_global"].mean()

    fig = plt.figure(figsize=(10, 16), constrained_layout=True)
    variants = list(DEFAULT_VARIANTS)
    batches = list(DEFAULT_BATCHES)
    subfigs = fig.subfigures(len(variants), 1, wspace=0.05, hspace=0.05)
    if len(variants) == 1:
        subfigs = [subfigs]
    else:
        subfigs = subfigs.flatten().tolist()

    legend_map = {2: "Global Lock (Ex 2)", 4: "Two Locks (Ex 4)", 5: "Lock-Free (Ex 5)"}
    color_map = {2: 'tab:blue', 4: 'tab:orange', 5: 'tab:green'}

    for subfig, variant in zip(subfigs, variants):
        subfig.suptitle(f"Variant {variant.upper()}", fontsize=14, fontweight='bold')
        axs = _as_list(subfig.subplots(1, 2, sharey=True))
        for ax, batch in zip(axs, batches):
            data = agg_df[(agg_df['variant'] == variant) & (agg_df['batch'] == batch)]
            if data.empty:
                _no_data(ax)
            for exercise in sorted(data['exercise'].unique()):
                ex_data = data[data['exercise'] == exercise]
                if not ex_data.empty and ex_data['max_freelist_size_global'].sum() > 0:
                    ax.plot(ex_data['NumThreads'], ex_data['max_freelist_size_global'], 
                            color=color_map.get(exercise, 'black'), marker='D', markersize=4, lw=2, 
                            label=legend_map.get(exercise, f"Ex {exercise}"))
            ax.set_title(f"Batch Size = {batch}", fontsize=10)
            ax.set_yscale("log") 
            ax.grid(True, which="both", ls="-", alpha=0.3)
            ax.set_xlabel("Number of Threads")
            _set_thread_axis(ax)
            ax.set_ylim(5e-1, 5e6)
            if batch == 1:
                ax.set_ylabel("Max Free List Size (#Nodes)")
            _safe_legend(ax, title="Exercise", loc='upper right', fontsize='x-small')

    plt.savefig(out_path)
    plt.close(fig)

def main():
    # CLI entrypoint: load CSVs, compute metrics, and save all plots.
    parser = argparse.ArgumentParser(description="Plot benchmark results")
    parser.add_argument("folder", type=Path, help="Folder containing CSV files")
    parser.add_argument("--output", "-o", type=Path, default=Path("plots"), help="Output folder for plots")
    args = parser.parse_args()

    if not args.folder.exists():
        print(f"Error: Folder {args.folder} does not exist.")
        sys.exit(1)
        
    out_dir = args.output
    out_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Loading data from {args.folder}...")
    try:
        raw = load_all_csvs(args.folder)
    except Exception as e:
        print(f"Error loading CSVs: {e}")
        sys.exit(1)
        
    print("Computing metrics...")
    run_df = per_run_metrics(raw)
    
    print(f"Saving plots to {out_dir}...")
    
    seq = run_df[run_df["impl"] == "seq"]
    if not seq.empty:
        plot_seq_throughput(seq, out_dir / "seq_throughput.png")

    try: plot_throughput(run_df, out_dir / "throughput.png")
    except Exception as e: print(f"Error plotting throughput: {e}")

    try: plot_speedup(run_df, out_dir / "speedup.png")
    except Exception as e: print(f"Error plotting speedup: {e}")
    
    try: plot_contention(run_df, out_dir / "contention_ex5.png")
    except Exception as e: print(f"Error plotting contention: {e}")

    try: plot_failed_cas_stack(run_df, out_dir / "failed_cas_stack.png")
    except Exception as e: print(f"Error plotting stack: {e}")

    try: plot_failed_ops_s(run_df, out_dir / "failed_ops_s.png")
    except Exception as e: print(f"Error plotting failed ops/s: {e}")

    try: plot_balance_scatter(raw, out_dir / "balance_scatter.png")
    except Exception as e: print(f"Error plotting scatter: {e}")

    try: plot_thread_imbalance(raw, out_dir / "thread_imbalance.png")
    except Exception as e: print(f"Error plotting imbalance: {e}")

    try: plot_freelist_size(run_df, out_dir / "freelist_size.png")
    except Exception as e: print(f"Error plotting freelist: {e}")

    print("Done.")

if __name__ == "__main__":
    main()
