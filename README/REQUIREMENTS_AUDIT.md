# Strict Requirements Audit

This file is the current compliance snapshot against `2502_COP290_A4.md`.

## Overall Status

The project is now aligned with the assignment API and the instructor tester:

- Arrow-backed storage and I/O are implemented for CSV and Parquet.
- Eager and lazy DataFrame operations are implemented across the required surface.
- The expression system covers arithmetic, comparison, boolean, string, null-check, and aggregation expressions.
- Lazy execution runs through a rule-based optimizer before materialization.
- The tester-facing API is exposed through `include/dataframelib/dataframelib.h`, the `dataframelib` namespace, and a CMake target named `dataframelib`.

## Public API Compatibility

The tester-specific compatibility points that now exist are:

- `dataframelib/dataframelib.h`
- `namespace dataframelib`
- CMake target `dataframelib`
- `EagerDataFrame::num_rows()` and `num_columns()`
- literal-friendly expression operators such as `col("age") > 30`
- brace-list friendly `select({"a", "b"})`
- `group_by(...).aggregate({{"salary", "sum"}})` style overloads
- `from_columns(...)` from Arrow arrays as well as strict `Column` objects
- string join kinds including `inner`, `left`, `right`, `full`, and `outer`

## Functional Coverage

Implemented eager operations:

- `select(columns)`
- `select(expressions)`
- `filter(predicate)`
- `with_column(name, expr)`
- `group_by(keys).aggregate(...)`
- `join(other, on, how)`
- `sort(columns, ascending)`
- `head(n)`
- `write_csv(path)`
- `write_parquet(path)`

Implemented lazy operations:

- `scan_csv(path)`
- `scan_parquet(path)`
- `select(...)`
- `filter(...)`
- `with_column(...)`
- `group_by(...).aggregate(...)`
- `join(...)`
- `sort(...)`
- `head(n)`
- `collect()`
- `sink_csv(path)`
- `sink_parquet(path)`
- `explain(path)`

## Execution Model

The eager and lazy layers now share one execution core instead of drifting apart:

- lazy queries build `LazyPlanNode` DAGs
- `collect()` runs the optimizer and then materializes the optimized plan
- eager expression-heavy operations reuse that same executor through an in-memory lazy source

That change removed the old asymmetry where `src/Lazy.cpp` had the real execution engine while `src/Eager.cpp` still had stubs.

## Optimizer Status

The optimizer is implemented and wired into the lazy path.

Implemented rule families:

- constant folding
- expression simplification
- predicate pushdown
- projection pushdown
- limit pushdown

`LazyDataFrame::collect()` optimizes before execution, and `LazyDataFrame::explain()` renders the optimized DAG.

## Verification

Verified locally with:

- `ctest --test-dir build --output-on-failure`
- `./.venv/bin/python tester/autograder.py --student-dir /home/bharti/project --data-dir /home/bharti/project/tester/results/test_data --output-dir /tmp/df_autograder_out --skip-build`

Current result:

- instructor autograder: `79 / 79` sub-tests passed

## Remaining Practical Note

The instructor autograder's fresh `Release` rebuild can exceed its default 300 second wrapper timeout on this machine. Prebuilding the tester tree and then using `--skip-build` avoids that packaging issue without changing library behavior.
