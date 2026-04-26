# Type System And Expression Pipeline

Relevant files:

- `include/DataFrameLib/Types.h`
- `src/Types.cpp`
- `include/DataFrameLib/Errors.h`
- `src/Errors.cpp`
- `include/DataFrameLib/ArrowUtils.h`
- `src/ArrowUtils.cpp`
- `include/DataFrameLib/Column.h`
- `src/Column.cpp`
- `include/DataFrameLib/Expression.h`
- `src/Expression.cpp`

## Supported Types

The logical type system is restricted to:

- `int32`
- `int64`
- `float32`
- `float64`
- `string`
- `boolean`

`DataType` and `LiteralValue` are the project-wide type boundary used by columns, expressions, and aggregation code.

## Null Semantics

Missing values are represented as Arrow nulls and surfaced as `NullType` in `LiteralValue`.

Current behavior:

- arithmetic, comparison, boolean, and string operators propagate nulls row-wise
- grouped aggregates ignore nulls for `count`, `sum`, `mean`, `min`, and `max` in the usual Arrow/DataFrame style
- `is_null()` and `is_not_null()` produce concrete boolean outputs rather than propagating nulls

That last point matters for filters such as `df.filter(col("x").is_null())`, which must keep null rows instead of dropping them.

## Type Validation

Type legality is enforced in three places:

1. schema validation at I/O and DataFrame construction boundaries
2. `Column`-level operation checks in `src/Column.cpp`
3. `Expression::infer_type(...)` before execution or optimization

Incompatible operations throw immediately through the project exception types in `Errors.h`.

## Numeric Promotion

Promotion rules are centralized in `src/Types.cpp`.

Current widening behavior includes:

- `int32 + int64 -> int64`
- `int32 + float32 -> float32`
- `int32 + float64 -> float64`
- `float32 + float64 -> float64`
- `int64 + float32 -> float64`

## Expression Surface

Supported expression families:

- `col(...)`
- `lit(...)`
- aliasing
- `+ - * / %`
- `== != < <= > >=`
- `& | ~`
- `is_null()`
- `is_not_null()`
- `abs()`
- `length()`
- `contains(...)`
- `starts_with(...)`
- `ends_with(...)`
- `to_lower()`
- `to_upper()`
- `sum()`
- `mean()`
- `count()`
- `min()`
- `max()`

Both API styles are supported:

- member style such as `col("salary").mean()`
- free-function style such as `mean(col("salary"))`

The public headers also provide literal-friendly operators so expressions like `col("age") > 30` and `col("department") == "Engineering"` work directly.

## Execution Path

Expression trees are symbolic until execution time.

They are evaluated by the shared plan executor in `src/Lazy.cpp`, which operates on top of the strict `Column` abstraction. Eager operations that need expression execution reuse that same path through `lazy_from_arrow_table(...)`, so eager and lazy semantics stay aligned.
