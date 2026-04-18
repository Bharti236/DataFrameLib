# Query Optimizer Design

This document explains the Step 5 design captured in `include/DataFrameLib/Optimizer.h`.

Important current status:

- `Optimizer.h` is now fully designed.
- `src/Optimizer.cpp` is still empty.
- `LazyDataFrame::collect()` currently executes the raw plan directly.
- `LazyDataFrame::explain()` currently renders the raw DAG directly.

So this file describes the intended optimization pipeline and how the designed API fulfills the assignment requirements once `src/Optimizer.cpp` is implemented.

## 1. Assignment Requirement to Header Mapping

The strict requirements say the project must provide a `QueryOptimizer` that optimizes lazy plans before materialization. The header now maps that requirement as follows:

- `QueryOptimizer::optimize(...)`
  This is the collect-time entry point.
- `QueryOptimizer::optimize_with_report(...)`
  This is the explain/report/viva entry point.
- `OptimizationRule`
  Enumerates the five required rule families.
- `OptimizerOptions`
  Lets us run all rules, disable selected rules for benchmarking, or compare optimized vs baseline plans.
- `OptimizationPassRecord` and `OptimizationReport`
  These give a concrete trace of which rules ran, how many rewrites happened, and what changed.

That last part is important because the assignment report must describe each optimization, argue correctness, give an example, and explain the expected performance benefit.

## 2. End-to-End Optimization Pipeline

The intended pipeline is:

1. `LazyDataFrame::collect()` calls `QueryOptimizer::optimize(plan())`.
2. The optimizer clones the logical DAG so the original lazy plan remains reusable.
3. The optimizer validates structural invariants of the plan.
4. The optimizer runs a fixpoint loop over the enabled rules.
5. After each pass, the optimizer may validate the rewritten plan again.
6. When no enabled rule changes the plan anymore, the optimized root is returned.
7. The lazy executor materializes that optimized root into an `EagerDataFrame`.

The intended `explain(path)` pipeline is similar:

1. Call `optimize_with_report(plan())`.
2. Render `report.optimized_root` instead of the raw root.
3. Optionally annotate the PNG or a side-channel text report with `report.passes`.

## 3. Planned Pass Order

The designed pass order in the header is:

1. Constant Folding
2. Expression Simplification
3. Predicate Pushdown
4. Projection Pushdown
5. Limit Pushdown

Why this order makes sense:

- Constant folding reduces literal-heavy expressions first.
- Expression simplification can then remove algebraic noise exposed by folding.
- Predicate pushdown benefits from simpler predicates.
- Projection pushdown benefits from cleaner expressions and predicates when computing required columns.
- Limit pushdown is safest as a later structural rewrite after the column/filter shape has stabilized.

The optimizer is designed to repeat this order until fixpoint, because one rewrite can expose another.

## 4. Rule-by-Rule Design

### Predicate Pushdown

Transformation:

- Move `Filter` nodes closer to scans.
- Split `a & b & c` into separate conjuncts.
- Push left-only predicates below the left side of a join.
- Push right-only predicates below the right side of a join.
- Keep mixed predicates above the join as residual filters.

Correctness idea:

- A predicate that depends only on columns from one child can be evaluated before the join without changing the final rows that survive.
- Splitting conjunctions is safe because `p & q` is equivalent to applying both filters.

Concrete example:

- Before: `Filter(age > 30, Join(employees, departments, {"dept_id"}))`
- After: `Join(Filter(age > 30, employees), departments, {"dept_id"})`

Expected benefit:

- Fewer rows enter joins and later operators.
- Lower join cost, lower memory pressure, lower downstream compute.

Header hooks:

- `apply_predicate_pushdown`
- `rewrite_predicates`
- `push_filter_through_unary_node`
- `push_filter_through_join`
- `split_conjunctive_predicate`
- `combine_conjunctive_predicates`
- `expression_uses_only_columns`
- `rewrite_expression_with_bindings`

### Projection Pushdown

Transformation:

- Walk required columns backward from the root.
- At each operator, compute which child columns are actually needed.
- Annotate scan nodes so they read only those columns.

Correctness idea:

- If a column is never referenced by any downstream expression, predicate, join key, group key, sort key, or final output, removing it cannot change the result.

Concrete example:

- Query uses only `name` and `salary`.
- Scan originally reads `id,name,dept,salary,city`.
- Optimized scan reads only `name,salary`.

Expected benefit:

- Less file I/O.
- Lower Arrow table width.
- Lower memory use and less copying/shuffling in later operators.

Header hooks:

- `apply_projection_pushdown`
- `rewrite_projections`
- `ColumnRequirement`
- `derive_child_requirement`
- `root_requirement`
- `annotate_scan_projection`
- `output_schema`
- `scan_schema`

Implementation note for this codebase:

- The current `LazyPlanNode` does not yet have a dedicated scan projection field.
- The design intentionally documents reuse of `LazyPlanNode::columns` on scan nodes to store required source columns.

### Constant Folding

Transformation:

- Replace constant-only expression subtrees with literal expressions.

Concrete examples:

- `lit(2) + lit(3)` becomes `lit(5)`
- `to_lower(lit("ABC"))` becomes `lit("abc")`
- `is_null(lit(nullptr))` becomes `lit(true)`

Correctness idea:

- A constant expression has the same value for every row and does not depend on input data.
- Evaluating it once at plan-construction/optimization time is semantically equivalent to evaluating it repeatedly at runtime.

Expected benefit:

- Less row-wise expression evaluation during execution.
- Simpler predicates and projections for later passes.

Header hooks:

- `apply_constant_folding`
- `rewrite_plan_expressions`
- `rewrite_expression`
- `evaluate_constant_expression`

### Expression Simplification

Transformation:

- Apply algebraic identities and remove trivial expressions.

Concrete examples:

- `x * 1` becomes `x`
- `x + 0` becomes `x`
- `filter(lit(true))` can become a no-op
- `head(head(n1), n2)` becomes `head(min(n1, n2))`

Correctness idea:

- These are standard expression identities that preserve value for all valid rows.
- They do not change null behavior when applied carefully.

Expected benefit:

- Smaller expression trees.
- Faster evaluation.
- More opportunities for pushdown in later passes.

Header hooks:

- `apply_expression_simplification`
- `rewrite_plan_expressions`
- `rewrite_expression`
- `simplify_expression`
- `preserve_alias`

### Limit Pushdown

Transformation:

- Merge adjacent `Head` nodes.
- Move `Head` below row-preserving unary operators when safe.

Concrete examples:

- `head(5, head(10, input))` becomes `head(5, input)`
- `head(10, select(exprs, input))` can become `select(exprs, head(10, input))`

Correctness idea:

- Moving `head(n)` is only safe across operators that preserve row order and row count up to the point where the limit is applied.
- It must not cross operators like filter, join, aggregate, or sort unless a stronger proof is available.

Expected benefit:

- Earlier row capping.
- Less work in projections and later materialization stages.

Header hooks:

- `apply_limit_pushdown`
- `rewrite_limits`
- `push_limit_through_unary_node`
- `merge_adjacent_limits`

## 5. Diagnostics and Benchmarking

The header deliberately includes:

- `OptimizerOptions`
- `OptimizationPassRecord`
- `OptimizationReport`

These serve three purposes:

1. Benchmarking
   We can disable all rules and measure the unoptimized baseline.
2. Viva/report support
   We can show which transformations happened on a sample query.
3. Debugging
   We can inspect whether a rule failed to trigger or over-triggered.

## 6. Planned Integration into Existing Lazy Code

The current lazy execution path is in `src/Lazy.cpp`. To finish Step 5 cleanly:

1. Implement `src/Optimizer.cpp` against `Optimizer.h`.
2. Change `LazyDataFrame::collect()` to:
   - construct `QueryOptimizer`,
   - optimize `plan_`,
   - execute the optimized root.
3. Change `LazyDataFrame::explain()` to render the optimized root.
4. Optionally expose `optimize_with_report()` output in a text explanation helper for viva/report preparation.

## 7. What This Design Already Solves

Even before the `.cpp` exists, the header now solves the design problem the project needed:

- all required optimization families are explicitly modeled,
- the pass order and fixpoint behavior are decided,
- the needed schema/column-analysis helpers are identified,
- the predicate/projection/limit rewrite boundaries are defined,
- and the reporting surface required by the assignment write-up is planned.

The remaining work is implementation, not architecture.
