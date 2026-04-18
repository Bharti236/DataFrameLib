# Strict Requirements Audit

This file is a direct verification pass against `2502_COP290_A4.md`.

The goal is to separate:

- what is already in sync with the assignment,
- what is only partially in sync,
- what is still missing.

## 1. Fully Or Mostly In Sync

### Arrow-backed storage and I/O

Status: mostly in sync

What is already present:

- Arrow is used as the storage layer.
- CSV read/write is implemented.
- Parquet read/write is implemented.
- eager read helpers exist as both free functions and static member wrappers.
- lazy `scan_csv` and `scan_parquet` build scan nodes without immediate I/O.
- lazy `sink_csv` and `sink_parquet` exist.

Main remaining note:

- projection pushdown into scans is not implemented yet, so scans still read full sources at execution time.

### Strict type system and null semantics

Status: in sync

What is already present:

- supported logical types are restricted correctly,
- null is the missing-value representation,
- numeric promotion rules exist,
- invalid type combinations throw immediately,
- column-level operations propagate nulls.

### Expression representation and validation

Status: mostly in sync

What is already present:

- symbolic expression tree,
- required core, arithmetic, comparison, boolean, string, and aggregation expression categories,
- type inference and column dependency tracking.

Recent alignment improvement:

- member-style expression helpers like `.alias()`, `.abs()`, `.to_lower()`, `.mean()` have now been added alongside the original free-function API.

### Lazy DAG construction

Status: mostly in sync

What is already present:

- lazy scan nodes,
- lazy select/filter/with_column/group_by/aggregate/join/sort/head nodes,
- deferred execution via `collect()`,
- Graphviz-based `explain(path)`,
- lazy sinks.

Main remaining note:

- the DAG is currently executed directly without an optimizer pass.

## 2. Partially In Sync

### Eager dataframe operations

Status: partially in sync

Implemented:

- `select(vector<string>)`
- `head(n)`
- `group_by(keys)` helper construction
- table/schema inspection helpers

Missing eager execution pieces:

- `select(vector<Expression>)`
- `filter(predicate)`
- `with_column(name, expr)`
- `group_by(...).aggregate(...)`
- `join(other, on, how)`
- `sort(columns, ascending)`

This is the biggest gap in the pre-Step-5 implementation.

### Public API exactness

Status: partially in sync

Improved now:

- free `from_columns(map)` helper has been added,
- member-style expression chaining helpers have been added,
- string-based join overloads have been added in eager and lazy APIs.

Still worth checking carefully before submission:

- the TA tests may expect exact signatures and exact header locations,
- the lazy layer is currently more complete than the eager layer,
- some convenience forms may still exist in addition to, not instead of, the exact public forms the TA intends.

## 3. Not Yet In Sync

### Query optimization

Status: not implemented yet

Current reality:

- `include/DataFrameLib/Optimizer.h` is now fully designed.
- `src/Optimizer.cpp` is empty.
- `LazyDataFrame::collect()` does not call a `QueryOptimizer`.
- `LazyDataFrame::explain()` renders the raw plan, not an optimized plan.

So Step 5 architecture is now specified, but Step 5 execution is not yet delivered.

### Optimizer report obligations

Status: not implemented yet

The assignment says each implemented optimization must be described with:

- the transformation,
- a correctness argument,
- a concrete example,
- an expected performance benefit.

The new `README/OPTIMIZER.md` and the diagnostics planned in `Optimizer.h` are designed to support that report, but the actual optimizer trace does not exist until `src/Optimizer.cpp` is implemented.

## 4. High-Risk Submission Gaps

If the project were submitted in the current state, the most likely grading risks are:

1. Eager dataframe operation tests would fail for the still-stubbed methods.
2. Query optimization grading would fail because the optimizer is not implemented.
3. Any test expecting `collect()` to optimize before execution would fail.
4. Any test expecting `explain()` to visualize the optimized DAG rather than the raw DAG could fail.

## 5. Practical Conclusion

The project is in a good state for:

- type safety,
- Arrow-backed columns,
- core expression modeling,
- lazy DAG construction,
- lazy execution,
- CSV/Parquet round-trips,
- Graphviz plan rendering.

The project is not yet in a good submission state for the full assignment because:

- eager operation parity is incomplete,
- the optimizer is only designed, not implemented.

## 6. Recommended Next Order Of Work

1. Implement `src/Optimizer.cpp` against the new `Optimizer.h`.
2. Wire the optimizer into `LazyDataFrame::collect()` and `explain()`.
3. Finish eager execution for expression projection, filter, with_column, join, sort, and grouped aggregate.
4. Add focused tests for exact public API usage and optimization rewrites.
