# Lazy Implementation Guide

This file explains the current implementation of the lazy layer in:

- `include/DataFrameLib/Lazy.h`
- `src/Lazy.cpp`

The goal is to help with:

- viva preparation,
- report writing,
- understanding why the code is structured this way,
- explaining the correctness and tradeoffs of the Step 4 implementation.

## 1. Assignment Goal

The assignment requires a lazy DataFrame API where:

- operations do not execute immediately,
- instead they build a computation DAG,
- leaf nodes are scans like `scan_csv()` and `scan_parquet()`,
- execution happens only when `collect()` is called,
- and `explain(path)` should render the DAG as a PNG.

That is exactly the design used here.

The lazy layer therefore has two responsibilities:

1. Represent the query as a DAG.
2. Materialize that DAG into an `EagerDataFrame` only at the execution boundary.

## 2. High-Level Design

The implementation separates lazy work into two phases.

### Phase A: Plan Construction

Methods such as:

- `select(...)`
- `filter(...)`
- `with_column(...)`
- `group_by(...)`
- `aggregate(...)`
- `join(...)`
- `sort(...)`
- `head(...)`

do not process data.

Instead, each method creates a new `LazyPlanNode`, links it to its input node(s), and returns a new `LazyDataFrame` wrapping the new root of the DAG.

### Phase B: Plan Execution

Execution is triggered only by:

- `collect()`
- `sink_csv(path)`
- `sink_parquet(path)`
- `explain(path)` for graph rendering

`collect()` walks the DAG recursively, executes dependencies first, and returns an `EagerDataFrame`.

## 3. Why `Lazy.h` Looks the Way It Does

### 3.1 `LazyNodeKind`

`LazyNodeKind` is the enum that classifies every logical operator in the plan.

Current values:

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

Why this is useful:

- the executor can switch on node kind,
- `explain()` can print meaningful labels,
- Step 5 optimizer rules can pattern-match on node kinds,
- the design stays close to relational algebra operators.

### 3.2 `LazyPlanNode`

This is the core DAG node structure.

Important fields:

- `id`: unique identifier for memoization and graph rendering.
- `kind`: which operation this node represents.
- `inputs`: parent nodes in the DAG.
- `path`: used by scan/sink nodes.
- `columns`: reused for column lists, group keys, sort keys, and join keys.
- `expressions`: used for expression-based projection.
- `predicate`: used for `filter`.
- `target_name`: used for `with_column`.
- `aggregations`: used for grouped aggregate output definitions.
- `join_type`: records inner/left/right/full join.
- `ascending`: records sort order.
- `limit`: records the argument for `head`.

Why a simple struct instead of a deep class hierarchy:

- it is easy to inspect,
- it is easy to serialize mentally in viva,
- it is easy to rewrite later in the optimizer,
- and the executor only needs to read fields, not dispatch virtual methods.

### 3.3 `LazyGroupBy`

`group_by(...)` does not aggregate immediately.

It returns a helper object `LazyGroupBy` that stores:

- a pointer to the parent `LazyDataFrame`,
- the grouping keys.

Then `aggregate(...)` creates:

1. a `GroupBy` node,
2. followed by an `Aggregate` node.

This matches the conceptual pipeline:

`input -> group_by(keys) -> aggregate(aggs)`

### 3.4 `LazyDataFrame`

`LazyDataFrame` is intentionally small. It only stores:

- `LazyPlanNodePtr plan_`

This means the entire lazy query is represented by a pointer to the root node of the DAG.

Every chaining operation returns a fresh `LazyDataFrame` with a new root node.

This gives immutability at the API level:

- old lazy plans remain usable,
- new operations extend the plan without mutating previous objects.

## 4. How Plan Construction Works in `Lazy.cpp`

The helper functions at the top of `src/Lazy.cpp` support plan building.

### `next_plan_node_id()`

Generates stable unique node IDs.

Why needed:

- `collect()` uses node IDs as memoization keys,
- `explain()` uses them as graph node names.

### `make_node(kind)`

Creates a node with:

- fresh ID,
- requested node kind.

### `make_unary_node(kind, input)`

Used for one-input operators like:

- select,
- filter,
- with_column,
- sort,
- head.

It ensures the input exists and then stores it in `inputs`.

### Source constructors

- `scan_csv(path)`
- `scan_parquet(path)`

These create leaf nodes only.

They do not open files and do not read data.

That is the key lazy property.

## 5. How Execution Works in `collect()`

### 5.1 Recursive execution model

`collect()` creates a cache and calls `execute_plan_impl(plan_, cache)`.

The executor:

1. checks whether the node result is already in cache,
2. recursively executes dependencies,
3. executes the current node,
4. stores the result in cache,
5. returns the resulting `EagerDataFrame`.

Why caching matters:

- if a subplan is shared by multiple downstream nodes,
- it is executed only once.

This matches DAG semantics better than naïve tree evaluation.

### 5.2 Scan nodes

- `ScanCsv` calls `read_csv(path)`
- `ScanParquet` calls `read_parquet(path)`

This is the first point where actual file I/O happens.

So the design satisfies the lazy requirement: scan methods are cheap during plan construction and only touch disk during execution.

## 6. Expression Evaluation Strategy

The current lazy executor includes a small expression evaluator because the eager layer does not yet implement general expression execution.

This evaluator lives in `src/Lazy.cpp` and works on top of the existing `Column` abstraction.

Important helpers:

- `evaluate_expression(...)`
- `evaluate_alias_child(...)`
- `default_expression_name(...)`
- `make_constant_column(...)`
- `make_all_null_column(...)`

### Supported expression categories

The evaluator supports:

- column references,
- literals,
- aliases,
- null checks,
- unary operations: `abs`, logical `not`,
- arithmetic: `+`, `-`, `*`, `/`, `%`,
- comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`,
- boolean ops: `&`, `|`,
- string ops: `length`, `to_lower`, `to_upper`, `contains`, `starts_with`, `ends_with`.

Aggregate expressions are handled separately in grouped execution.

### Why evaluate via `Column`

This is a good design choice for the current project state because:

- Step 1 already implemented null-aware typed column operations,
- those operations already enforce type safety,
- those operations already preserve null semantics,
- so the lazy executor reuses the existing, tested behavior instead of reimplementing type logic again.

### Null semantics

Null handling follows the assignment rules:

- null operands produce null outputs,
- filter keeps only rows where predicate is `true`,
- `false` and `null` rows are both dropped.

For literal nulls:

- row-wise operations create all-null result columns,
- `is_null(null)` becomes all `true`,
- `is_not_null(null)` becomes all `false`.

## 7. Operator-by-Operator Execution

### 7.1 `SelectColumns`

Execution simply delegates to eager `select(vector<string>)`.

Why this is correct:

- projection by name is already implemented eagerly,
- this preserves order and schema validation.

### 7.2 `SelectExpressions`

Execution:

1. evaluates each expression into a `Column`,
2. gives it a final output name,
3. builds a new `EagerDataFrame` from those columns.

Output naming rule:

- use alias if present,
- otherwise derive from the expression type,
- and reject duplicate output names.

### 7.3 `Filter`

Execution:

1. evaluate predicate to a boolean `Column`,
2. compute row indices where value is `true`,
3. take those rows from every input column.

Rows with `null` predicate values are excluded.

That is a safe and assignment-consistent choice.

### 7.4 `WithColumn`

Execution:

1. evaluate expression into a `Column`,
2. verify output length matches input row count,
3. replace the existing column if the name already exists,
4. otherwise append the new column.

This preserves the “add or replace” behavior expected by the assignment.

### 7.5 `GroupBy + Aggregate`

This is implemented in `execute_aggregate(...)`.

Execution steps:

1. execute the input before the `GroupBy` node,
2. collect grouping key columns,
3. assign rows to groups using a serialized key string,
4. materialize each group as a smaller `EagerDataFrame`,
5. compute each aggregate expression per group,
6. build the final grouped output table.

Supported aggregates:

- `sum`
- `mean`
- `count`
- `min`
- `max`

Current `count` behavior:

- counts non-null values only.

This matches the assignment table.

Why serialized group keys are used:

- simple to implement,
- works across numeric/string/boolean keys,
- deterministic and easy to explain in viva.

Tradeoff:

- not the fastest approach,
- but perfectly reasonable for Step 4 before optimizer/performance work in Step 5.

### 7.6 `Join`

This is implemented in `execute_join(...)`.

Supported join types:

- inner
- left
- right
- full

Execution strategy:

1. execute both inputs,
2. extract join-key columns from both sides,
3. compare every left row with every right row,
4. append matching rows according to join semantics,
5. add unmatched side rows for left/right/full joins.

Naming rule:

- keep all left columns,
- add right non-key columns,
- if a right column name conflicts, append `_right`.

Null join semantics:

- if either side has null in a join key, that pair does not match.

Tradeoff:

- this is an `O(n*m)` nested-loop join,
- simple and correct,
- but not optimized yet.

That is acceptable for Step 4; Step 5 is where optimization becomes important.

### 7.7 `Sort`

This is implemented in `execute_sort(...)`.

Execution:

1. build row indices `[0..n-1]`,
2. stable-sort them using the requested sort columns,
3. reorder the table by taking rows in sorted index order.

Current null placement:

- nulls are always pushed to the end.

The sort is stable:

- if all sort keys compare equal, original row order is preserved.

### 7.8 `Head`

Execution delegates to eager `head(n)`.

This is correct and keeps the implementation small.

## 8. Graph Rendering with `explain(path)`

`explain(path)` uses Graphviz C APIs:

- `gvContext()`
- `agopen(...)`
- `agnode(...)`
- `agedge(...)`
- `gvLayout(...)`
- `gvRenderFilename(...)`

The helper `add_graphviz_subgraph(...)` recursively visits the DAG and:

- creates one graph node per logical node,
- connects dependency edges,
- labels nodes using `make_node_label(...)`.

The label includes useful information such as:

- node kind,
- path,
- column list,
- target name,
- join type,
- limit,
- expression count,
- aggregate count.

This makes the PNG useful during viva because you can visually explain:

- where the scans are,
- how operators depend on each other,
- and how the final plan is structured.

## 9. Important Utility Helpers

### `dataframe_columns(df)`

Extracts all columns from an `EagerDataFrame`.

Useful when reconstructing new output tables.

### `dataframe_from_columns(columns, row_count)`

Builds a validated `EagerDataFrame` from a vector of `Column`s.

This is used repeatedly by:

- projection,
- filtering,
- with-column,
- joins,
- grouping.

### `take_rows(df, indices)`

Applies a row selection to all columns.

This is the common primitive behind:

- filter,
- join output construction,
- grouped sub-frames,
- sort reordering.

## 10. Correctness Story

If asked “why is this correct?”, this is the shortest good answer:

1. Lazy operations do not execute immediately; they only append DAG nodes.
2. `collect()` evaluates dependencies first, so execution respects data dependencies.
3. Expression evaluation reuses the existing typed `Column` layer, so type safety and null semantics are preserved.
4. Operators such as filter, projection, with-column, group-by aggregate, join, sort, and head are implemented in a way that matches the assignment’s required semantics.
5. `explain()` traverses the same DAG structure that `collect()` executes, so the rendered graph reflects the real plan.

## 11. Complexity and Tradeoffs

This implementation is correct and educational, but not yet optimized.

### Current strengths

- clean DAG representation,
- immutable user-facing lazy chaining,
- execution only at boundary methods,
- easy to reason about in viva,
- good basis for Step 5 optimizer work.

### Current tradeoffs

- joins use nested loops,
- grouping uses serialized hash keys,
- many operations rebuild columns via `LiteralValue` vectors,
- no predicate pushdown or projection pushdown yet,
- no optimized physical execution plan yet.

These are all acceptable for Step 4 and become natural motivation for Step 5.

## 12. What Step 5 Can Improve

This implementation intentionally leaves good hooks for the optimizer.

Examples:

- `LazyNodeKind` makes rewrite rules easy.
- `plan()` exposes the root DAG.
- `Head` nodes can be pushed downward.
- `Filter` nodes can be pushed toward scans.
- `SelectColumns` and `SelectExpressions` can drive projection pushdown.
- constant expressions can be folded before execution.

So the current Step 4 work is not wasted; it is the foundation for Step 5.

## 13. Likely Viva Questions

### “Why use a DAG instead of directly storing a list of operations?”

Because joins and shared subplans naturally create multiple dependencies, which are better represented as a DAG than a simple chain.

### “Why is `group_by()` a separate helper instead of aggregating immediately?”

Because grouping and aggregation are conceptually different operations. The helper matches the public API and keeps the logical plan explicit: first group, then aggregate.

### “Why store nodes with `shared_ptr`?”

Because multiple downstream nodes may depend on the same upstream node, and `shared_ptr` gives safe shared ownership of the logical plan.

### “Why execute expressions through `Column` instead of Arrow compute directly?”

Because the project already has a strict typed `Column` abstraction with null propagation and type checks. Reusing it keeps semantics consistent and reduces duplicated logic.

### “Why is the join not optimized?”

Because Step 4 is about correctness and lazy DAG construction. Performance optimization is the focus of Step 5.

### “What guarantees laziness here?”

All chainable methods only create nodes. File I/O and materialization occur only in `collect()`, `sink_csv()`, `sink_parquet()`, and `explain()`.

## 14. Suggested Report Points

If you want to turn this into report text, the most useful themes are:

- The lazy layer separates logical planning from physical execution.
- Each `LazyDataFrame` stores only the root of a DAG.
- `collect()` is a recursive interpreter over logical plan nodes.
- The execution engine reuses the strict `Column` abstraction from Step 1 for correctness.
- The current implementation prioritizes clear semantics and optimizer-readiness over raw performance.
- This directly supports future rule-based optimization in Step 5.

## 15. One-Sentence Summary

The lazy layer works by representing every user operation as a node in a shared DAG, deferring all I/O and computation until `collect()`, and then interpreting that DAG into concrete `EagerDataFrame` results while preserving the type-safety and null semantics already implemented in the core column system.
