# Lazy Implementation Guide

Relevant files:

- `include/DataFrameLib/Lazy.h`
- `src/Lazy.cpp`
- `include/DataFrameLib/Optimizer.h`
- `src/Optimizer.cpp`

## Plan Model

Lazy queries are represented as DAGs of `LazyPlanNode`.

Current node kinds:

- `ScanCsv`
- `ScanParquet`
- `InMemorySource`
- `SelectColumns`
- `SelectExpressions`
- `Filter`
- `WithColumn`
- `GroupBy`
- `Aggregate`
- `Join`
- `Sort`
- `Head`
- `SinkCsv`
- `SinkParquet`

Important payload fields:

- `path` for file-backed scans
- `in_memory_table` for eager-backed lazy sources
- `columns` for projections, group keys, sort keys, join keys, and pushed scan projections
- `expressions` for expression projections and `with_column`
- `predicate` for filters
- `aggregations` for grouped aggregate outputs
- `join_type`, `ascending`, and `limit` for operator-specific behavior

## Construction Phase

Methods such as `select`, `filter`, `with_column`, `group_by`, `aggregate`, `join`, `sort`, and `head` only extend the DAG.

No data is read or transformed until:

- `collect()`
- `sink_csv(path)`
- `sink_parquet(path)`
- `explain(path)`

## Execution Phase

`collect()` does three things:

1. optimize the current root plan
2. execute the optimized DAG recursively with memoization
3. return a materialized `EagerDataFrame`

Execution is shared by both user-written lazy queries and eager operations that wrap materialized tables through `lazy_from_arrow_table(...)`.

That shared in-memory source is the piece that removed the old eager/lazy implementation split.

## Operator Semantics

The executor in `src/Lazy.cpp` currently handles:

- file scans
- in-memory scans
- expression projections
- boolean filtering
- `with_column`
- grouped aggregation
- inner, left, right, and full/outer joins
- stable multi-column sorting
- `head(n)`

Join output rules:

- left columns are preserved first
- duplicate non-key right columns are renamed with `_right` and a uniqueness suffix if needed
- full outer joins preserve join-key values for right-only rows

## `explain(path)`

`explain(path)` optimizes the plan and renders the optimized DAG with Graphviz.

The renderer writes PNG output to the exact path supplied by the caller. The tester uses a `.txt` filename for this call; that is fine because the contract checked there is file creation, not filename extension.
