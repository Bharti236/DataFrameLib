# Query Optimizer

Relevant files:

- `include/DataFrameLib/Optimizer.h`
- `src/Optimizer.cpp`

## Current Status

The optimizer is implemented and active.

- `LazyDataFrame::collect()` calls `QueryOptimizer::optimize(...)`
- `LazyDataFrame::explain()` calls `QueryOptimizer::optimize_with_report(...)`

The optimizer clones and rewrites the logical DAG instead of mutating the original lazy query in place.

## Implemented Rule Families

1. Constant folding
2. Expression simplification
3. Predicate pushdown
4. Projection pushdown
5. Limit pushdown

The pass order is still the intended one from the header design:

1. constant folding
2. expression simplification
3. predicate pushdown
4. projection pushdown
5. limit pushdown

and the optimizer runs that order to a fixpoint.

## What Each Rule Does Here

Constant folding:

- reduces literal-only expression subtrees before execution

Expression simplification:

- removes trivial identities such as arithmetic no-ops and redundant filters/limits

Predicate pushdown:

- pushes filters toward scans when the predicate depends only on the relevant child columns
- can split conjunctive predicates and keep residual filters above joins when needed

Projection pushdown:

- tracks required columns backward from the root
- annotates scan and in-memory source nodes with the smallest safe column set

Limit pushdown:

- merges adjacent `head(...)` nodes
- pushes row limits through row-preserving unary operators when safe

## Diagnostics

`OptimizationReport` and `OptimizationPassRecord` are populated by `optimize_with_report(...)`.

That reporting surface is useful for:

- viva explanations
- report writing
- debugging rewrite behavior
- comparing optimized vs less-optimized plans

## Conservative Boundaries

The optimizer is intentionally conservative around operators where moving work can change semantics:

- joins
- aggregates
- sorts
- filters with mixed-child dependencies

That conservatism is deliberate; correctness wins over a more aggressive but fragile rewrite.
