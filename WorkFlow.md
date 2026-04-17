Step 2: Create a Modular Directory Structure
Organize your project to separate the eager logic, the lazy evaluation graph, and the expression system.

DataFrameLib/\
├── CMakeLists.txt\
├── include/\
│   ├── DataFrameLib/\
│   │   ├── Types.h        # Strict type definitions (int32, string, etc.)\
│   │   ├── Eager.h        # EagerDataFrame class\
│   │   ├── Lazy.h         # LazyDataFrame & DAG classes\
│   │   └── Optimizer.h    # QueryOptimizer rules\
├── src/\
│   ├── Eager.cpp\
│   ├── Lazy.cpp\
│   └── Optimizer.cpp\
└── tests/

Step 3: Configure strict CMake constraints
To enforce the strict type safety required for this assignment, configuring aggressive compiler flags in your standard CMakeLists.txt is highly recommended. You must also link Arrow and Graphviz.

# Go to build directory
```bash
mkdir -p build && cd build
```

# Configure and Compile
```bash
cmake ..
make
```

# Run your program
```bash
./df_app
```

### **Phase 3: Implementation Outline**

I highly recommend building this incrementally. Do not try to write the optimizer before the eager DataFrame works!

#### **Step 1: Core Data Structures & Type Safety**
* Create your base column structures using Arrow.
* [cite_start]Enforce strict immutable types: `int32`, `int64`, `float32`, `float64`, `string`, and `boolean`[cite: 33].
* [cite_start]Implement null handling (using strict `null` representation, not `NaN`)[cite: 34]. [cite_start]Ensure operations involving a null operand produce a null result[cite: 37].
* [cite_start]Implement type promotion rules (e.g., `int` + `float` $\rightarrow$ `float`) and throw immediate errors for incompatible operations[cite: 35, 36].

Step 1 is really about building a strong type system, null semantics, and Arrow column wrappers. The eager/lazy APIs should sit on top of that, not mix into it.

DataFrameLib/\
├── CMakeLists.txt\
├── include/\
│   └── DataFrameLib/\
│       ├── Types.h\
│       ├── Errors.h\
│       ├── ArrowUtils.h\
│       ├── Column.h\
│       ├── Expression.h\
│       ├── IO.h\
│       ├── Eager.h\
│       ├── Lazy.h\
│       └── Optimizer.h\
├── src/\
│   ├── Types.cpp\
│   ├── Errors.cpp\
│   ├── ArrowUtils.cpp\
│   ├── Column.cpp\
│   ├── Expression.cpp\
│   ├── IO.cpp\
│   ├── Eager.cpp\
│   ├── Lazy.cpp\
│   └── Optimizer.cpp\
└── tests/

```cpp
// ===============================
// Step 1 implementation order
// ===============================
//
// 1) Types.h / Types.cpp
//    Define the supported logical types, promotion rules, and compatibility checks.
//
// 2) Errors.h / Errors.cpp
//    Define a clear exception hierarchy and uniform error helpers.
//
// 3) ArrowUtils.h / ArrowUtils.cpp
//    Connect the logical type system to Arrow scalars, arrays, and casts.
//
// 4) Column.h / Column.cpp
//    Build the immutable Arrow-backed column abstraction with null propagation.
//
// 5) Expression.h / Expression.cpp
//    Build the typed symbolic expression layer used by later DataFrame APIs.
```
```cpp
// ============================================================
// Step 2 implementation order
// ============================================================
//
// 1) IO.h / IO.cpp
//    Get CSV and Parquet loading/writing working with strict types.
//
// 2) Eager.h / Eager.cpp
//    Implement construction, schema validation, select, filter, with_column,
//    and head first.
//
// 3) Grouping support
//    Add group_by and aggregate after basic row-wise operations are stable.
//
// 4) Join and sort
//    Add the more complex row-reordering and row-combining operations last.
//
// 5) Add tests
//    Test null propagation, promotion, schema errors, and I/O round-trips.
```

#### **Step 2: I/O Functions & Eager DataFrame**
* [cite_start]Implement the `EagerDataFrame` class[cite: 19].
* [cite_start]Build the I/O functions to load CSV and Parquet files into memory (`read_csv`, `read_parquet`) and write them back out (`write_csv`, `write_parquet`)[cite: 43].
* [cite_start]Implement the core operations eagerly: `select`, `filter`, `with_column`, `group_by`, `aggregate`, `join`, `sort`, and `head`[cite: 46].

#### **Step 3: The Expression System**
* Build an Abstract Syntax Tree (AST) structure to represent operations without executing them immediately.
* [cite_start]Implement Core Expressions: `col`, `lit`, and `alias`[cite: 49].
* [cite_start]Implement Arithmetic (`+`, `-`, `*`, `/`, `%`, `abs`), Comparison/Boolean (`==`, `>`, `&`, `is_null`, etc.), String (`length`, `starts_with`, `to_lower`, etc.), and Aggregation (`sum`, `mean`, `count`, etc.) expressions[cite: 51, 53, 57, 60].

#### **Step 4: Lazy DataFrame & DAG Construction**
* [cite_start]Implement the `LazyDataFrame` class[cite: 20].
* [cite_start]Instead of processing data, make your operations (`scan_csv`, `filter`, `join`, etc.) append nodes to a computation DAG[cite: 62, 63, 64].
* [cite_start]Implement the `explain(path)` method using Graphviz/Boost.Graph to dump a visual representation (`.png`) of your DAG[cite: 70].
* [cite_start]Implement the `collect()` method, which triggers actual materialization of the graph into results[cite: 69].

#### **Step 5: The Query Optimizer (Crucial for Performance Grades)**
* [cite_start]Build the `QueryOptimizer` to transform the unoptimized DAG before `collect()` executes it[cite: 21, 24].
* [cite_start]Implement baseline rules: Predicate Pushdown, Projection Pushdown, Constant Folding, Expression Simplification (e.g., transforming `x * 1 + 0` to `x`), and Limit Pushdown[cite: 78].
* [cite_start]*Note:* Performance is graded relative to other submissions and an unoptimized baseline[cite: 76].

---

### **Phase 4: Finalizing & Submission**

* [cite_start]**Design Document (`report.pdf`):** You must document your architecture and prove the correctness of each optimization you implemented, including concrete examples and expected performance benefits[cite: 79, 123].
* [cite_start]**Code Quality:** Ensure your public API exactly matches the provided document signatures[cite: 115]. [cite_start]Ensure proper memory management (smart pointers, RAII, move semantics) to avoid memory leaks (worth 15% of the grade)[cite: 110].
* [cite_start]**Packaging:** Place all files in a `project` directory[cite: 131]. [cite_start]Run `tar -cvf <entry_number>.tar project` (e.g., `2025ANZ8223.tar`) from the parent directory[cite: 132, 133, 134]. [cite_start]Submit via Moodle[cite: 136].

---

### **Pro-Tips for Success**
1.  [cite_start]**Strict API Compliance:** The grading is largely automated (45% for correctness)[cite: 110]. [cite_start]If your function names or signatures deviate from the assignment document, tests will fail[cite: 115].
2.  [cite_start]**Memory is Money:** You get graded on memory leaks and proper use of C++ semantics[cite: 110]. Use tools like Valgrind or AddressSanitizer during development.
3.  [cite_start]**Prepare for the Viva:** 15% of your grade is a live viva/demo[cite: 110]. [cite_start]Even if your code works perfectly, a poor viva can drastically reduce your marks[cite: 116, 129]. Make sure you deeply understand *why* your query optimizer works, not just *how* you coded it.

# Implimentation wise:

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

If you want, I can turn this into a very concrete “file-by-file checklist” for Step 1 only, with each function named exactly as a TODO item.
