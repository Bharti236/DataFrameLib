This is a good point to lock the architecture down before writing code. The assignment wants Arrow-backed storage/I/O, strict immutable column types, null-only missing values, immediate errors for incompatible ops, both eager and lazy DataFrames, and a lazy DAG that can be rendered with `explain(path)`; it also requires the exact public API to match the spec closely. 

I would keep your top-level structure, but split the “core” layer a bit more cleanly so Step 1 does not get tangled with the DataFrame classes.

## Recommended file layout

```text
DataFrameLib/
├── CMakeLists.txt
├── include/
│   └── DataFrameLib/
│       ├── Types.h
│       ├── Errors.h
│       ├── ArrowUtils.h
│       ├── Column.h
│       ├── Expression.h
│       ├── IO.h
│       ├── Eager.h
│       ├── Lazy.h
│       └── Optimizer.h
├── src/
│   ├── Types.cpp
│   ├── Errors.cpp
│   ├── ArrowUtils.cpp
│   ├── Column.cpp
│   ├── Expression.cpp
│   ├── IO.cpp
│   ├── Eager.cpp
│   ├── Lazy.cpp
│   └── Optimizer.cpp
└── tests/
```

The reason for this split is simple: Step 1 is really about building a strong type system, null semantics, and Arrow column wrappers. The eager/lazy APIs should sit on top of that, not mix into it.

---

## `include/DataFrameLib/Types.h`

This file should define all strict type rules and the tiny type-related helpers that the rest of the project depends on.

### What goes here

* A `DataType` enum with exactly these logical types:
  `Int32`, `Int64`, `Float32`, `Float64`, `String`, `Boolean`.
* A `Null` representation for literals and internal expression values.
* A `LiteralValue` variant type for scalar constants.
* Type-checking helpers.
* Type-promotion helpers.

### Functions to define

* `is_numeric(DataType)`
  Returns true for integer and floating-point types.

* `is_integral(DataType)`
  Returns true for `int32` and `int64`.

* `is_floating(DataType)`
  Returns true for `float32` and `float64`.

* `is_string(DataType)` / `is_boolean(DataType)`
  Convenience checks.

* `type_name(DataType)`
  Returns a human-readable name for error messages.

* `promote_types(DataType left, DataType right)`
  Implements promotion like `int + float -> float`.
  This is the central rule for arithmetic expressions.

* `can_compare(DataType left, DataType right)`
  Tells whether a comparison is legal.

* `validate_binary_op(op, left, right)`
  Throws immediately if the operation is invalid.

### Responsibility

This file should not know anything about DataFrames. It only decides what types are legal, what can be combined, and what the result type should be. That keeps the rest of the code much cleaner.

---

## `include/DataFrameLib/Errors.h`

This should contain your project-specific exceptions.

### What goes here

* `DataFrameError` as a base exception.
* `TypeError`
* `NullError` if you want a separate category.
* `SchemaError`
* `ColumnNotFoundError`
* `DimensionError`
* `PlanError`
* `IOError`

### Responsibility

Every time the user does something invalid, you throw one of these with a precise message. That makes debugging and TA tests much easier.

---

## `include/DataFrameLib/ArrowUtils.h`

This is your Arrow adapter layer. It keeps Arrow boilerplate out of the rest of the code.

### What goes here

* Conversions between your `LiteralValue` and Arrow scalars.
* Builders for arrays and tables.
* Helpers for null masks.
* Helpers for safe casting.

### Functions to define

* `make_arrow_scalar(LiteralValue)`
  Converts a literal into an Arrow scalar.

* `make_null_scalar(DataType)`
  Creates a typed null scalar.

* `cast_array(array, target_type)`
  Performs Arrow casting when a promotion is needed.

* `concat_arrays(...)`

* `take_rows(...)`

* `filter_rows(...)`

* `slice_rows(...)`

### Responsibility

Every actual column operation should ultimately use Arrow objects. This file is where you centralize that logic.

---

## `include/DataFrameLib/Column.h`

This is the most important file for Step 1.

### What goes here

A thin immutable wrapper around a single Arrow column.

### What the class should represent

* Column name
* Column type
* Arrow storage (`arrow::Array` or `arrow::ChunkedArray`)
* Null bitmap / null count handled by Arrow, not manually through `NaN`

### Functions / methods to define

* Constructor from name, type, and Arrow data.

* `name()`

* `type()`

* `size()`

* `null_count()`

* `is_null_at(row_index)`

* `rename(new_name)`
  Returns a new Column, does not mutate.

* `cast_to(DataType target)`
  Used for type promotion.

* `slice(offset, length)`
  Returns a new Column view/materialized slice.

* `take(indices)`
  Row selection helper.

* `filter(mask_column)`
  Returns rows where the boolean mask is true.

* `apply_unary(op)`

* `apply_binary(op, other_column)`

* `compare(op, other_column)`

* `logical_and`, `logical_or`, `logical_not` if you keep them separate.

* String helpers:
  `string_length()`, `contains()`, `starts_with()`, `ends_with()`, `to_lower()`, `to_upper()`

### Null behavior rule

Any operation involving a null operand must produce a null result. That should be enforced here, because this is where row-wise array computation actually happens. The assignment is explicit that missing values are represented as null only, not `NaN`. 

### Responsibility

This file should make columns immutable and type-safe. No in-place mutation. Every transformation returns a new Column.

---

## `include/DataFrameLib/Expression.h`

This file should define the expression tree used by both eager and lazy mode.

The assignment requires column refs, literals, aliasing, arithmetic, comparisons, boolean ops, string functions, and aggregations like `sum`, `mean`, `count`, `min`, and `max`. 

### What goes here

* An `Expression` class or node wrapper.
* Node kinds such as:
  `ColumnRef`, `Literal`, `Alias`, `UnaryOp`, `BinaryOp`, `ComparisonOp`, `BooleanOp`, `StringOp`, `AggregateOp`, `IsNull`, `IsNotNull`.

### Factory functions / builders

* `col(name)`
* `lit(value)`
* `alias(name)`

### Operator-style expression builders

* Arithmetic: `+`, `-`, `*`, `/`, `%`, `abs()`
* Comparison: `==`, `!=`, `<`, `<=`, `>`, `>=`
* Boolean: `&`, `|`, `~`
* String: `length()`, `contains()`, `starts_with()`, `ends_with()`, `to_lower()`, `to_upper()`
* Null checks: `is_null()`, `is_not_null()`
* Aggregates: `sum()`, `mean()`, `count()`, `min()`, `max()`

### Core methods

* `infer_type(schema)`
  Checks validity and determines the result type.

* `evaluate(table_or_row_batch)`
  Produces an Arrow array for the expression.

* `required_columns()`
  Returns which input columns this expression needs.

* `is_constant()`

* `is_aggregate()`

* `to_string()`
  Useful for debugging and optimizer explanations.

### Responsibility

This is where type safety rules get enforced at expression-construction or expression-evaluation time. Incompatible operations should fail fast, not silently coerce.

---

## `include/DataFrameLib/IO.h`

This file should expose the assignment’s I/O functions in one place. The assignment explicitly lists `read_csv`, `read_parquet`, `scan_csv`, `scan_parquet`, `write_csv`, `write_parquet`, `sink_csv`, `sink_parquet`, and `from_columns(map)` as required operations. 

### What goes here

Free functions or static factory methods for:

* `read_csv(path)` → `EagerDataFrame`
* `read_parquet(path)` → `EagerDataFrame`
* `scan_csv(path)` → `LazyDataFrame`
* `scan_parquet(path)` → `LazyDataFrame`
* `from_columns(map)` → `EagerDataFrame` or shared helper object
* `write_csv(path)` on eager frames
* `write_parquet(path)` on eager frames
* `sink_csv(path)` on lazy frames
* `sink_parquet(path)` on lazy frames

### Responsibility

This file should only define public entry points and delegate to Arrow-based loaders/savers.

---

## `include/DataFrameLib/Eager.h`

This file should define the immediate-execution DataFrame API.

The assignment says eager mode should execute immediately and materialize results. 

### What goes here

* `class EagerDataFrame`

### Core methods

* Constructors from Arrow table / column map / helper factory.
* `select(...)`
* `filter(...)`
* `with_column(name, expr)`
* `group_by(keys)`
* `join(other, on, how)`
* `sort(columns, asc)`
* `head(n)`
* `write_csv(path)`
* `write_parquet(path)`

### Group-by design

`group_by(keys)` should not aggregate immediately. It should return a lightweight grouping handle that then supports:

* `aggregate(agg_map)`

That mirrors the usage in the assignment examples. 

### Responsibility

All of these methods should return new materialized eager frames. No hidden laziness.

---

## `include/DataFrameLib/Lazy.h`

This file should define the deferred-execution DAG model.

The assignment says `LazyDataFrame` builds a DAG, has leaf scan nodes, does no processing until `collect()`, and supports `explain(path)` to dump the DAG as a PNG. 

### What goes here

* `class LazyDataFrame`
* A `PlanNode` hierarchy, or a single node structure with an enum kind and child pointers.

### Node kinds you need

* `ScanCsvNode`
* `ScanParquetNode`
* `SelectNode`
* `FilterNode`
* `WithColumnNode`
* `GroupByNode`
* `AggregateNode`
* `JoinNode`
* `SortNode`
* `HeadNode`

### Core methods

* `select(...)`
* `filter(...)`
* `with_column(...)`
* `group_by(...)`
* `join(...)`
* `sort(...)`
* `head(...)`
* `collect()`
* `explain(path)`

### `collect()` responsibility

Materialize the whole plan into an `EagerDataFrame`.

### `explain(path)` responsibility

Dump the DAG to a `.png` file using Graphviz or Boost.Graph, exactly as the assignment allows. 

### Responsibility

This file is only about plan construction and execution triggering. It should not do heavy relational work itself.

---

## `include/DataFrameLib/Optimizer.h`

This file should contain rule-based optimization for lazy plans.

The assignment explicitly mentions predicate pushdown, projection pushdown, constant folding, expression simplification, and limit pushdown. 

### What goes here

* `class QueryOptimizer`
* `optimize(plan)` as the main entry point.

### Rule functions to define

* `predicate_pushdown(plan)`
* `projection_pushdown(plan)`
* `constant_folding(plan)`
* `expression_simplification(plan)`
* `limit_pushdown(plan)`

### Helper functions

* `collect_required_columns(plan)`
* `split_conjunctive_predicate(expr)`
* `fold_expression(expr)`
* `simplify_expression(expr)`
* `push_filter_through_join(...)`
* `push_projection_through_scan(...)`

### Responsibility

Take a lazy DAG and rewrite it into an equivalent but cheaper DAG before execution.

---

## What Step 1 should actually implement first

For your current milestone, I would complete these files first:

1. `Types.h / Types.cpp`
2. `Errors.h / Errors.cpp`
3. `ArrowUtils.h / ArrowUtils.cpp`
4. `Column.h / Column.cpp`
5. `Expression.h / Expression.cpp`

That gives you strict typing, null semantics, promotion rules, and a reliable Arrow-backed column/expression layer. Once that is solid, `EagerDataFrame` becomes much easier, and `LazyDataFrame` can reuse the exact same expression system.

---

## Suggested implementation order

Start with the smallest safe core:

* type enum and promotion rules,
* error classes,
* Arrow conversion helpers,
* immutable column wrapper,
* expression tree with validation.

Then move upward:

* eager dataframe,
* lazy dataframe,
* optimizer,
* DAG visualization.

# File Structure:

Yes. The safest way is to keep the headers layered so lower-level files do not include higher-level ones.

```text
Types.h
  └─ included by almost everything

Errors.h
  └─ includes Types.h

ArrowUtils.h
  ├─ includes Types.h
  └─ includes Errors.h

Column.h
  ├─ includes Types.h
  ├─ includes Errors.h
  └─ includes ArrowUtils.h

Expression.h
  ├─ includes Types.h
  └─ includes Errors.h

IO.h
  ├─ includes Types.h
  └─ forward-declares EagerDataFrame

Eager.h
  ├─ includes Types.h
  ├─ includes Errors.h
  ├─ includes Column.h
  └─ includes Expression.h

Lazy.h
  ├─ includes Types.h
  ├─ includes Errors.h
  ├─ includes Expression.h
  └─ forward-declares EagerDataFrame

Optimizer.h
  ├─ includes Types.h
  ├─ includes Errors.h
  ├─ includes Expression.h
  └─ includes Lazy.h
```

### What each file should not include

`Types.h`
Should not include any of the other project headers.

`Errors.h`
Should not include `Column.h`, `Expression.h`, `Eager.h`, `Lazy.h`, or `Optimizer.h`.

`ArrowUtils.h`
Should not include `Column.h`, `Eager.h`, `Lazy.h`, or `Optimizer.h`.

`Column.h`
Should not include `Eager.h` or `Lazy.h`.
Try to keep it independent of `Expression.h` too, unless you absolutely need expression-based column methods there.

`Expression.h`
Should not include `Column.h`, `Eager.h`, `Lazy.h`, or `Optimizer.h`.

`IO.h`
Should not include `Eager.h`. Use a forward declaration instead.

`Eager.h`
Should not include `IO.h`.
This avoids a cycle, because `IO.h` refers to `EagerDataFrame`.

`Lazy.h`
Should not include `Eager.h` or `IO.h`.
Use forward declaration of `EagerDataFrame` for `collect()`.

`Optimizer.h`
Should not include `IO.h` or `Eager.h` unless a specific optimizer rule truly needs it. Usually it only needs the lazy plan and expression AST.

### Best include rule

A good rule is:

* low-level headers include only low-level headers
* high-level headers include low-level headers
* use forward declarations for cross-references
* keep `.cpp` files responsible for pulling in the heavier dependencies

### Clean dependency order

```text
Types.h
  ↓
Errors.h
  ↓
ArrowUtils.h
  ↓
Column.h
  ↓
Expression.h
  ↓
Eager.h
  ↓
Lazy.h
  ↓
Optimizer.h
```

That is the cleanest mental model. It keeps circular includes away and makes the project much easier to compile.
