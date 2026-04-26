#!/usr/bin/env python3
"""
Autograder for COP290 Assignment 4 — DataFrameLib.

Generates test data, builds student code, runs C++ test programs,
and validates outputs against Pandas-computed expected results.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

from generate_data import generate_all

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

TEST_PROGRAMS = [
    "test_io",
    "test_select_filter",
    "test_groupby_agg",
    "test_join",
    "test_expressions",
    "test_string_ops",
    "test_sort_head",
    "test_with_column",
    "test_lazy",
    "test_null_types",
    "test_edge_cases",
    "test_operators",
    "test_advanced_join",
    "test_chained_ops",
    "test_advanced_lazy",
    "test_performance",
    "test_from_columns",
]

TIMEOUT_SECONDS = 180


# ---------------------------------------------------------------------------
# DataFrame comparison
# ---------------------------------------------------------------------------


def _normalize_df(df: pd.DataFrame) -> pd.DataFrame:
    """Normalize a DataFrame for comparison: sort by all columns, reset index."""
    cols = list(df.columns)
    try:
        df_sorted = df.sort_values(by=cols, na_position="last").reset_index(drop=True)
    except TypeError:
        df_sorted = df.reset_index(drop=True)
    return df_sorted


def compare_dataframes(
    actual_path: str,
    expected_df: pd.DataFrame,
    float_tolerance: float = 1e-5,
    order_matters: bool = False,
) -> tuple[bool, str]:
    """Compare a CSV on disk with an expected Pandas DataFrame.

    Returns (passed, message).
    """
    if not os.path.exists(actual_path):
        return False, f"Output file not found: {actual_path}"

    try:
        actual = pd.read_csv(actual_path)
    except Exception as e:
        return False, f"Failed to read output CSV: {e}"

    if actual.shape[0] != expected_df.shape[0]:
        return False, (
            f"Row count mismatch: got {actual.shape[0]}, expected {expected_df.shape[0]}"
        )

    if actual.shape[1] != expected_df.shape[1]:
        return False, (
            f"Column count mismatch: got {actual.shape[1]}, expected {expected_df.shape[1]}"
        )

    # Column name check (order-insensitive)
    actual_cols = set(actual.columns)
    expected_cols = set(expected_df.columns)
    if actual_cols != expected_cols:
        missing = expected_cols - actual_cols
        extra = actual_cols - expected_cols
        msg_parts = []
        if missing:
            msg_parts.append(f"missing columns: {missing}")
        if extra:
            msg_parts.append(f"extra columns: {extra}")
        return False, "Column mismatch — " + "; ".join(msg_parts)

    # Reorder actual columns to match expected
    actual = actual[expected_df.columns]

    if not order_matters:
        actual = _normalize_df(actual)
        expected_df = _normalize_df(expected_df)
    else:
        actual = actual.reset_index(drop=True)
        expected_df = expected_df.reset_index(drop=True)

    for col in expected_df.columns:
        a = actual[col]
        e = expected_df[col]

        # Null positions must match
        a_null = a.isna()
        e_null = e.isna()
        if not (a_null == e_null).all():
            n_diff = (a_null != e_null).sum()
            return False, f"Column '{col}': null positions differ in {n_diff} rows"

        # Compare non-null values
        mask = ~e_null
        if mask.sum() == 0:
            continue

        a_vals = a[mask].reset_index(drop=True)
        e_vals = e[mask].reset_index(drop=True)

        if pd.api.types.is_float_dtype(e_vals) or pd.api.types.is_float_dtype(a_vals):
            try:
                a_f = a_vals.astype(float)
                e_f = e_vals.astype(float)
                if not np.allclose(a_f, e_f, rtol=float_tolerance, atol=1e-9, equal_nan=True):
                    max_diff = np.max(np.abs(a_f - e_f))
                    return False, f"Column '{col}': float values differ (max diff={max_diff:.6g})"
            except (ValueError, TypeError):
                if not (a_vals.values == e_vals.values).all():
                    return False, f"Column '{col}': values differ"
        else:
            a_s = a_vals.astype(str)
            e_s = e_vals.astype(str)
            if not (a_s.values == e_s.values).all():
                n_diff = (a_s.values != e_s.values).sum()
                return False, f"Column '{col}': {n_diff} values differ"

    return True, "OK"


# ---------------------------------------------------------------------------
# Expected result computation (mirrors C++ test operations)
# ---------------------------------------------------------------------------


def compute_expected_results(data_dir: str) -> dict[str, pd.DataFrame]:
    """Compute all expected outputs using Pandas. Returns {filename: DataFrame}."""
    results: dict[str, pd.DataFrame] = {}

    data = pd.read_csv(os.path.join(data_dir, "data.csv"))
    left = pd.read_csv(os.path.join(data_dir, "left.csv"))
    right = pd.read_csv(os.path.join(data_dir, "right.csv"))
    string_data = pd.read_csv(os.path.join(data_dir, "string_data.csv"))
    null_data = pd.read_csv(os.path.join(data_dir, "null_data.csv"))

    # --- test_io ---
    results["io_csv_roundtrip.csv"] = data.copy()
    results["io_parquet_as_csv.csv"] = data.copy()
    results["io_from_parquet.csv"] = data.copy()

    # --- test_select_filter ---
    results["select_result.csv"] = data[["name", "salary"]].copy()
    results["filter_gt.csv"] = data[data["age"] > 30].copy()
    results["filter_eq.csv"] = data[data["department"] == "Engineering"].copy()
    results["filter_compound.csv"] = data[
        (data["age"] > 25) & (data["salary"] > 50000.0)
    ].copy()
    results["filter_select.csv"] = (
        data[data["age"] > 30][["name", "salary", "department"]].copy()
    )

    # --- test_groupby_agg ---
    gb = data.groupby("department", sort=False).agg(
        salary_sum=("salary", "sum"),
        salary_mean=("salary", "mean"),
        age_min=("age", "min"),
        age_max=("age", "max"),
        id_count=("id", "count"),
    ).reset_index()
    results["groupby_result.csv"] = gb

    gb_multi = data.groupby(["department", "city"], sort=False).agg(
        salary_mean=("salary", "mean"),
        id_count=("id", "count"),
    ).reset_index()
    results["groupby_multi.csv"] = gb_multi

    # --- test_join ---
    results["join_inner.csv"] = pd.merge(left, right, on="id", how="inner")
    results["join_left.csv"] = pd.merge(left, right, on="id", how="left")

    # --- test_expressions ---
    expr_bonus = data.copy()
    expr_bonus["bonus"] = expr_bonus["salary"] * 0.1
    results["expr_bonus.csv"] = expr_bonus

    expr_abs = data.copy()
    expr_abs["abs_val"] = (expr_abs["salary"] - 60000.0).abs()
    results["expr_abs.csv"] = expr_abs

    expr_cf = data[
        ((data["salary"] > 50000.0) & (data["age"] < 40)) | (data["department"] == "HR")
    ].copy()
    results["expr_complex_filter.csv"] = expr_cf

    expr_chain = data.copy()
    expr_chain["computed"] = (expr_chain["salary"] + expr_chain["age"] * 100) / 2.0
    results["expr_arith_chain.csv"] = expr_chain

    # --- test_string_ops ---
    results["contains_result.csv"] = string_data[
        string_data["email"].str.contains("@gmail.com", na=False)
    ].copy()

    upper_df = string_data.copy()
    upper_df["name_upper"] = upper_df["name"].str.upper()
    results["upper_result.csv"] = upper_df

    lower_df = string_data.copy()
    lower_df["name_lower"] = lower_df["name"].str.lower()
    results["lower_result.csv"] = lower_df

    results["startswith_result.csv"] = string_data[
        string_data["code"].str.startswith("A", na=False)
    ].copy()

    results["endswith_result.csv"] = string_data[
        string_data["filename"].str.endswith(".txt", na=False)
    ].copy()

    length_df = string_data.copy()
    length_df["name_len"] = length_df["name"].str.len()
    results["length_result.csv"] = length_df

    # --- test_sort_head ---
    results["sort_asc.csv"] = data.sort_values("salary", ascending=True).copy()
    results["sort_desc.csv"] = data.sort_values("salary", ascending=False).copy()
    results["head_result.csv"] = data.head(5).copy()
    results["sort_head.csv"] = (
        data.sort_values("salary", ascending=False).head(5).copy()
    )

    # --- test_with_column ---
    wc_doubled = data.copy()
    wc_doubled["salary_doubled"] = wc_doubled["salary"] * 2
    results["wc_doubled.csv"] = wc_doubled

    wc_sum = data.copy()
    wc_sum["total"] = wc_sum["salary"] + wc_sum["age"]
    results["wc_sum.csv"] = wc_sum

    # --- test_lazy (same expected results as eager counterparts) ---
    results["lazy_filter.csv"] = data[data["age"] > 30].copy()
    results["lazy_select.csv"] = data[["name", "salary"]].copy()

    lazy_gb = data.groupby("department", sort=False).agg(
        salary_sum=("salary", "sum"),
        salary_mean=("salary", "mean"),
        id_count=("id", "count"),
    ).reset_index()
    results["lazy_groupby.csv"] = lazy_gb

    lazy_chain = (
        data[data["salary"] > 50000.0][["name", "salary", "department"]]
        .sort_values("salary", ascending=False)
        .copy()
    )
    results["lazy_chain.csv"] = lazy_chain
    results["lazy_sink.csv"] = data[data["age"] > 30].copy()

    # --- test_null_types ---
    results["is_null_result.csv"] = null_data[null_data["x"].isna()].copy()
    results["is_not_null_result.csv"] = null_data[null_data["x"].notna()].copy()

    null_arith = null_data.copy()
    null_arith["sum_xy"] = null_arith["x"] + null_arith["y"]
    results["null_arith.csv"] = null_arith

    nn = null_data[null_data["x"].notna() & null_data["y"].notna()].copy()
    nn["product"] = nn["x"] * nn["y"]
    results["null_filter_compute.csv"] = nn

    # --- test_edge_cases ---
    results["edge_empty_filter.csv"] = data[data["age"] > 200].copy()
    results["edge_head_zero.csv"] = data.head(0).copy()
    results["edge_head_large.csv"] = data.head(5000).copy()
    results["edge_select_single.csv"] = data[["name"]].copy()
    results["edge_replace_col.csv"] = data.assign(age=data["age"] + 1)
    results["edge_filter_all.csv"] = data[data["age"] > 0].copy()

    hr = data[data["department"] == "HR"]
    results["edge_single_group.csv"] = hr.groupby("department", sort=False).agg(
        salary_sum=("salary", "sum"),
        salary_count=("salary", "count"),
    ).reset_index()

    results["edge_double_sort.csv"] = (
        data.sort_values("salary").sort_values(["age", "id"], ascending=False).copy()
    )

    # --- test_operators ---
    sel_ida = data[["id", "salary", "age"]].copy()
    results["ops_subtract.csv"] = sel_ida.assign(diff=sel_ida["salary"] - sel_ida["age"])

    sel_is = data[["id", "salary"]].copy()
    results["ops_divide.csv"] = sel_is.assign(monthly=sel_is["salary"] / 12.0)

    sel_id_only = data[["id"]].copy()
    results["ops_modulo.csv"] = sel_id_only.assign(id_mod=sel_id_only["id"] % 10)

    results["ops_less.csv"] = data[data["age"] < 25].copy()
    results["ops_less_equal.csv"] = data[data["age"] <= 25].copy()
    results["ops_greater_equal.csv"] = data[data["age"] >= 60].copy()
    results["ops_not_equal.csv"] = data[data["department"] != "Engineering"].copy()
    results["ops_not.csv"] = data[~(data["age"] > 30)].copy()

    results["ops_nested_arith.csv"] = sel_ida.assign(
        v=((sel_ida["salary"] - 50000.0) * 2.0 + sel_ida["age"] * 100) / 3.0
    )
    results["ops_bool_combo.csv"] = data[
        ((data["age"] >= 30) & ~(data["department"] == "HR"))
        | (data["salary"] > 100000.0)
    ].copy()

    # --- test_advanced_join ---
    results["join_outer.csv"] = pd.merge(left, right, on="id", how="outer")
    results["join_empty_inner.csv"] = pd.merge(
        left[left["id"] > 10000], right, on="id", how="inner"
    )
    results["join_self.csv"] = pd.merge(
        data[["id", "name"]], data[["id", "salary", "department"]],
        on="id", how="inner",
    )
    results["join_right_nulls.csv"] = pd.merge(
        left, right[right["id"] < 50], on="id", how="left"
    )

    # --- test_chained_ops ---
    results["chain_filter_sort_head.csv"] = (
        data[data["salary"] > 50000.0]
        .sort_values(["age", "id"], ascending=True)
        .head(10)
        .copy()
    )

    tmp_bonus = data.assign(bonus=data["salary"] * 0.1)
    tmp_bonus = tmp_bonus[tmp_bonus["bonus"] > 10000.0]
    results["chain_wc_filter_gb.csv"] = tmp_bonus.groupby(
        "department", sort=False
    ).agg(
        salary_sum=("salary", "sum"),
        bonus_mean=("bonus", "mean"),
    ).reset_index()

    results["chain_multi_filter.csv"] = data[
        (data["age"] > 25) & (data["salary"] > 50000.0) & (data["department"] != "HR")
    ].copy()

    results["chain_select_wc.csv"] = data[["id", "name", "salary"]].assign(
        double_salary=data["salary"] * 2
    )

    pipeline = data[data["age"] >= 30].copy()
    pipeline["annual_bonus"] = pipeline["salary"] * 0.15
    pipeline = pipeline[["name", "department", "salary", "annual_bonus"]]
    pipeline = pipeline.sort_values(["salary", "name"], ascending=False).head(20)
    results["chain_full_pipeline.csv"] = pipeline

    # --- test_advanced_lazy (same expected outputs as eager equivalents) ---
    results["lazy_join.csv"] = pd.merge(left, right, on="id", how="inner")
    results["lazy_with_column.csv"] = data.assign(bonus=data["salary"] * 0.1)
    results["lazy_sort_head.csv"] = (
        data.sort_values("salary", ascending=False).head(10).copy()
    )
    results["lazy_predicate_push.csv"] = (
        data[data["age"] > 30][["name", "salary", "age"]].copy()
    )
    results["lazy_full_pipeline.csv"] = pipeline.copy()

    # --- test_performance (large_data.csv) ---
    large = pd.read_csv(os.path.join(data_dir, "large_data.csv"))
    results["perf_filter.csv"] = large[large["value1"] > 5000.0].copy()
    results["perf_groupby.csv"] = large.groupby("category", sort=False).agg(
        value1_sum=("value1", "sum"),
        value2_mean=("value2", "mean"),
        id_count=("id", "count"),
    ).reset_index()
    results["perf_sort.csv"] = large.sort_values("value1").head(100).copy()
    perf_lazy = (
        large[large["value1"] > 0.0]
        .sort_values("value2", ascending=False)
        .head(50)
        .copy()
    )
    results["perf_lazy_chain.csv"] = perf_lazy

    # --- test_from_columns (in-memory columns; not read from data_dir) ---
    fc_simple = pd.DataFrame(
        {
            "id": [1, 2, 3, 4, 5],
            "name": ["a", "b", "c", "d", "e"],
            "score": [10.5, 20.0, 30.25, 40.0, 50.5],
        }
    )
    results["fc_simple.csv"] = fc_simple.copy()
    results["fc_filter.csv"] = fc_simple[fc_simple["score"] > 25.0].copy()
    results["fc_with_column.csv"] = fc_simple.assign(total=fc_simple["score"] * 2.0)
    results["fc_empty.csv"] = pd.DataFrame(
        {"id": pd.Series([], dtype="int64"), "name": pd.Series([], dtype=object)}
    )
    results["fc_select.csv"] = fc_simple[["id", "score"]].copy()

    return results


# Mapping: test program → list of output CSV filenames it produces
TEST_OUTPUTS: dict[str, list[str]] = {
    "test_io": ["io_csv_roundtrip.csv", "io_parquet_as_csv.csv", "io_from_parquet.csv"],
    "test_select_filter": [
        "select_result.csv",
        "filter_gt.csv",
        "filter_eq.csv",
        "filter_compound.csv",
        "filter_select.csv",
    ],
    "test_groupby_agg": ["groupby_result.csv", "groupby_multi.csv"],
    "test_join": ["join_inner.csv", "join_left.csv"],
    "test_expressions": [
        "expr_bonus.csv",
        "expr_abs.csv",
        "expr_complex_filter.csv",
        "expr_arith_chain.csv",
    ],
    "test_string_ops": [
        "contains_result.csv",
        "upper_result.csv",
        "lower_result.csv",
        "startswith_result.csv",
        "endswith_result.csv",
        "length_result.csv",
    ],
    "test_sort_head": ["sort_asc.csv", "sort_desc.csv", "head_result.csv", "sort_head.csv"],
    "test_with_column": ["wc_doubled.csv", "wc_sum.csv"],
    "test_lazy": [
        "lazy_filter.csv",
        "lazy_select.csv",
        "lazy_groupby.csv",
        "lazy_chain.csv",
        "lazy_sink.csv",
    ],
    "test_null_types": [
        "is_null_result.csv",
        "is_not_null_result.csv",
        "null_arith.csv",
        "null_filter_compute.csv",
    ],
    "test_edge_cases": [
        "edge_empty_filter.csv",
        "edge_head_zero.csv",
        "edge_head_large.csv",
        "edge_select_single.csv",
        "edge_replace_col.csv",
        "edge_filter_all.csv",
        "edge_single_group.csv",
        "edge_double_sort.csv",
    ],
    "test_operators": [
        "ops_subtract.csv",
        "ops_divide.csv",
        "ops_modulo.csv",
        "ops_less.csv",
        "ops_less_equal.csv",
        "ops_greater_equal.csv",
        "ops_not_equal.csv",
        "ops_not.csv",
        "ops_nested_arith.csv",
        "ops_bool_combo.csv",
    ],
    "test_advanced_join": [
        "join_outer.csv",
        "join_empty_inner.csv",
        "join_self.csv",
        "join_right_nulls.csv",
    ],
    "test_chained_ops": [
        "chain_filter_sort_head.csv",
        "chain_wc_filter_gb.csv",
        "chain_multi_filter.csv",
        "chain_select_wc.csv",
        "chain_full_pipeline.csv",
    ],
    "test_advanced_lazy": [
        "lazy_join.csv",
        "lazy_with_column.csv",
        "lazy_sort_head.csv",
        "lazy_predicate_push.csv",
        "lazy_full_pipeline.csv",
    ],
    "test_performance": [
        "perf_filter.csv",
        "perf_groupby.csv",
        "perf_sort.csv",
        "perf_lazy_chain.csv",
    ],
    "test_from_columns": [
        "fc_simple.csv",
        "fc_filter.csv",
        "fc_with_column.csv",
        "fc_empty.csv",
        "fc_select.csv",
    ],
}

# Tests where row order matters (sort-related)
ORDER_MATTERS = {
    "sort_asc.csv",
    "sort_desc.csv",
    "head_result.csv",
    "sort_head.csv",
    "lazy_chain.csv",
    "edge_double_sort.csv",
    "chain_filter_sort_head.csv",
    "chain_full_pipeline.csv",
    "lazy_sort_head.csv",
    "lazy_full_pipeline.csv",
    "perf_sort.csv",
    "perf_lazy_chain.csv",
}


# ---------------------------------------------------------------------------
# Build helpers
# ---------------------------------------------------------------------------


def _detect_cmake_prefix_path() -> str:
    """Try to detect Arrow's install prefix from the active conda env or system paths."""
    conda_prefix = os.environ.get("CONDA_PREFIX", "")
    if conda_prefix:
        candidate = os.path.join(conda_prefix, "lib", "cmake", "Arrow", "ArrowConfig.cmake")
        if os.path.isfile(candidate):
            return conda_prefix

    try:
        import pyarrow
        pa_dir = os.path.dirname(pyarrow.__file__)
        for parent in [pa_dir, os.path.join(pa_dir, "..")]:
            for sub in ["lib/cmake/Arrow", "share/arrow"]:
                candidate = os.path.join(parent, sub, "ArrowConfig.cmake")
                if os.path.isfile(os.path.abspath(candidate)):
                    return os.path.abspath(parent)
    except ImportError:
        pass

    return ""


def build_student(
    student_dir: str, test_programs_dir: str, build_dir: str
) -> tuple[bool, str]:
    """Configure and build the student library + test programs. Returns (ok, msg)."""
    os.makedirs(build_dir, exist_ok=True)

    cmake_cmd = [
        "cmake",
        "-S", test_programs_dir,
        "-B", build_dir,
        f"-DSTUDENT_DIR={os.path.abspath(student_dir)}",
        "-DCMAKE_BUILD_TYPE=Release",
    ]

    prefix = _detect_cmake_prefix_path()
    if prefix:
        cmake_cmd.append(f"-DCMAKE_PREFIX_PATH={prefix}")
    print(f"  CMake configure: {' '.join(cmake_cmd)}")
    result = subprocess.run(
        cmake_cmd, capture_output=True, text=True, timeout=120,
    )
    if result.returncode != 0:
        return False, f"CMake configure failed:\n{result.stderr}\n{result.stdout}"

    build_cmd = ["cmake", "--build", build_dir, "--parallel"]
    print(f"  CMake build: {' '.join(build_cmd)}")
    result = subprocess.run(
        build_cmd, capture_output=True, text=True, timeout=300,
    )
    if result.returncode != 0:
        return False, f"CMake build failed:\n{result.stderr}\n{result.stdout}"

    return True, "Build succeeded"


# ---------------------------------------------------------------------------
# Test runner
# ---------------------------------------------------------------------------


class SubTestResult:
    def __init__(self, name: str, passed: bool, message: str):
        self.name = name
        self.passed = passed
        self.message = message


class TestResult:
    def __init__(self, program: str):
        self.program = program
        self.compiled = False
        self.ran = False
        self.exit_code: Optional[int] = None
        self.runtime_ms: float = 0.0
        self.stdout: str = ""
        self.stderr: str = ""
        self.subtests: list[SubTestResult] = []
        self.timing: dict[str, float] = {}
        self.error: Optional[str] = None

    @property
    def passed_count(self) -> int:
        return sum(1 for s in self.subtests if s.passed)

    @property
    def failed_count(self) -> int:
        return sum(1 for s in self.subtests if not s.passed)

    @property
    def total_count(self) -> int:
        return len(self.subtests)


def parse_stdout(stdout: str) -> tuple[dict[str, float], list[tuple[str, bool, str]]]:
    """Parse TIMING and PASS/ERROR lines from test program stdout."""
    timings: dict[str, float] = {}
    results: list[tuple[str, bool, str]] = []

    for line in stdout.strip().splitlines():
        line = line.strip()
        if line.startswith("TIMING:"):
            parts = line.split(":", 2)
            if len(parts) == 3:
                try:
                    timings[parts[1]] = float(parts[2])
                except ValueError:
                    pass
        elif line.startswith("PASS:"):
            name = line[5:]
            results.append((name, True, ""))
        elif line.startswith("ERROR:"):
            parts = line.split(":", 2)
            name = parts[1] if len(parts) > 1 else "unknown"
            msg = parts[2] if len(parts) > 2 else ""
            results.append((name, False, msg))

    return timings, results


def run_test(
    program: str,
    build_dir: str,
    data_dir: str,
    output_dir: str,
    expected: dict[str, pd.DataFrame],
) -> TestResult:
    """Run a single test program and validate its outputs."""
    result = TestResult(program)

    binary = os.path.join(build_dir, program)
    if not os.path.isfile(binary):
        result.error = f"Binary not found: {binary}"
        return result

    result.compiled = True
    test_output_dir = os.path.join(output_dir, program)
    os.makedirs(test_output_dir, exist_ok=True)

    try:
        t0 = time.time()
        proc = subprocess.run(
            [binary, data_dir, test_output_dir],
            capture_output=True,
            text=True,
            timeout=TIMEOUT_SECONDS,
        )
        result.runtime_ms = (time.time() - t0) * 1000
        result.exit_code = proc.returncode
        result.stdout = proc.stdout
        result.stderr = proc.stderr
        result.ran = True
    except subprocess.TimeoutExpired:
        result.error = f"Timed out after {TIMEOUT_SECONDS}s"
        return result
    except Exception as e:
        result.error = str(e)
        return result

    result.timing, stdout_results = parse_stdout(proc.stdout)

    # Check stdout-reported errors
    stdout_reported: dict[str, tuple[bool, str]] = {}
    for name, ok, msg in stdout_results:
        stdout_reported[name] = (ok, msg)

    # Validate output CSVs against expected
    output_files = TEST_OUTPUTS.get(program, [])
    for fname in output_files:
        subtest_name = fname.replace(".csv", "")
        fpath = os.path.join(test_output_dir, fname)

        if fname not in expected:
            result.subtests.append(SubTestResult(
                subtest_name, False, "No expected result computed (internal error)"
            ))
            continue

        order_matters = fname in ORDER_MATTERS
        passed, msg = compare_dataframes(fpath, expected[fname], order_matters=order_matters)
        result.subtests.append(SubTestResult(subtest_name, passed, msg))

    # For test_lazy, also check explain (just that it didn't crash)
    if program == "test_lazy":
        if "lazy_explain" in stdout_reported:
            ok, msg = stdout_reported["lazy_explain"]
            result.subtests.append(SubTestResult("lazy_explain", ok, msg))
        else:
            explain_path = os.path.join(test_output_dir, "lazy_explain.txt")
            passed = os.path.isfile(explain_path)
            result.subtests.append(SubTestResult(
                "lazy_explain", passed,
                "OK" if passed else "explain output file not found",
            ))

    return result


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


def print_summary(results: list[TestResult]) -> tuple[int, int]:
    """Print results table. Returns (total_passed, total_tests)."""

    sep = "=" * 95
    print(f"\n{sep}")
    print(f"{'TEST RESULTS':^95}")
    print(sep)
    print(
        f"{'Test Program':<28} {'Sub-tests':>10} {'Passed':>8} {'Failed':>8} "
        f"{'Time(ms)':>10} {'Status':>12}"
    )
    print("-" * 95)

    grand_passed = 0
    grand_total = 0

    for r in results:
        if r.error and not r.ran:
            status = "ERROR"
            print(
                f"{r.program:<28} {'—':>10} {'—':>8} {'—':>8} "
                f"{'—':>10} {status:>12}"
            )
            print(f"    -> {r.error}")
            continue

        total = r.total_count
        passed = r.passed_count
        failed = r.failed_count
        grand_passed += passed
        grand_total += total
        time_ms = f"{r.runtime_ms:.1f}"
        if total > 0 and failed == 0:
            status = "ALL PASS"
        elif total > 0:
            status = f"{passed}/{total} PASS"
        else:
            status = "NO TESTS"

        print(
            f"{r.program:<28} {total:>10} {passed:>8} {failed:>8} "
            f"{time_ms:>10} {status:>12}"
        )

        for s in r.subtests:
            mark = "+" if s.passed else "X"
            detail = "" if s.passed else f" -- {s.message}"
            print(f"    {mark} {s.name}{detail}")

        if r.exit_code and r.exit_code != 0:
            print(f"    (exit code: {r.exit_code})")

    print(sep)
    print(f"\nTotal: {grand_passed} / {grand_total} sub-tests passed")
    print(sep)

    return grand_passed, grand_total


def write_json_report(results: list[TestResult], passed: int, total: int, path: str) -> None:
    """Write a machine-readable JSON report."""
    report = {
        "passed": passed,
        "total": total,
        "tests": [],
    }
    for r in results:
        entry = {
            "program": r.program,
            "compiled": r.compiled,
            "ran": r.ran,
            "exit_code": r.exit_code,
            "runtime_ms": round(r.runtime_ms, 2),
            "error": r.error,
            "subtests": [
                {"name": s.name, "passed": s.passed, "message": s.message}
                for s in r.subtests
            ],
            "timing": r.timing,
        }
        report["tests"].append(entry)

    with open(path, "w") as f:
        json.dump(report, f, indent=2)
    print(f"\nJSON report written to {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(
        description="COP290 A4 — DataFrameLib Autograder",
    )
    parser.add_argument(
        "--student-dir", required=True,
        help="Path to the student's project root (must contain CMakeLists.txt)",
    )
    parser.add_argument(
        "--output-dir", default="results",
        help="Directory for autograder outputs (default: results/)",
    )
    parser.add_argument(
        "--data-dir", default=None,
        help="Directory with pre-generated test data (skip generation if set)",
    )
    parser.add_argument(
        "--skip-build", action="store_true",
        help="Skip the build step (use existing build directory)",
    )
    parser.add_argument(
        "--tests", nargs="*", default=None,
        help="Run only specified tests (e.g. test_io test_join)",
    )
    args = parser.parse_args()

    student_dir = os.path.abspath(args.student_dir)
    output_dir = os.path.abspath(args.output_dir)
    tester_dir = os.path.dirname(os.path.abspath(__file__))
    test_programs_dir = os.path.join(tester_dir, "test_programs")

    if not os.path.isdir(student_dir):
        print(f"ERROR: Student directory not found: {student_dir}")
        sys.exit(1)

    os.makedirs(output_dir, exist_ok=True)

    # --- 1. Generate test data ---
    if args.data_dir:
        data_dir = os.path.abspath(args.data_dir)
        print(f"Using pre-generated data from {data_dir}")
    else:
        data_dir = os.path.join(output_dir, "test_data")
        generate_all(data_dir)

    # --- 2. Build ---
    build_dir = os.path.join(output_dir, "build")
    if not args.skip_build:
        print("\nBuilding student code and test programs...")
        ok, msg = build_student(student_dir, test_programs_dir, build_dir)
        if not ok:
            print(f"\nBUILD FAILED:\n{msg}")
            sys.exit(2)
        print("  Build successful.\n")
    else:
        print("Skipping build step.\n")

    # --- 3. Compute expected results ---
    print("Computing expected results with Pandas...")
    expected = compute_expected_results(data_dir)
    print(f"  Computed {len(expected)} expected output DataFrames.\n")

    # --- 4. Run tests ---
    programs = args.tests if args.tests else TEST_PROGRAMS
    test_output_dir = os.path.join(output_dir, "test_outputs")
    os.makedirs(test_output_dir, exist_ok=True)

    all_results: list[TestResult] = []
    for prog in programs:
        if prog not in TEST_PROGRAMS:
            print(f"WARNING: Unknown test program '{prog}', skipping.")
            continue
        print(f"Running {prog}...")
        r = run_test(prog, build_dir, data_dir, test_output_dir, expected)
        all_results.append(r)

    # --- 5. Report ---
    passed, total = print_summary(all_results)
    json_path = os.path.join(output_dir, "report.json")
    write_json_report(all_results, passed, total, json_path)

    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()
