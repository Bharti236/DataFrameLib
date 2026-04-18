# Type System And Expression Pipeline

This file groups the assignment requirements related to:

- strict type safety
- null semantics
- the `Column` abstraction
- the expression system

Relevant source files:

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

## 1. Type Safety Pipeline

The assignment requires immutable column types restricted to:

- `int32`
- `int64`
- `float32`
- `float64`
- `string`
- `boolean`

Current implementation path:

1. `DataType` is the project-wide logical type enum.
2. `LiteralValue` is the strict scalar variant used by columns and expressions.
3. `ArrowUtils` converts between project types and Arrow types.
4. `Column` validates that its declared logical type matches the Arrow array type.
5. `EagerDataFrame` and I/O validation reject unsupported Arrow schemas.
6. `Expression::infer_type(...)` enforces type legality before execution.

This fulfills the intended design rule that incompatible operations should fail early.

## 2. Null Semantics Pipeline

The assignment requires:

- missing values are null only,
- not NaN,
- any operation involving a null operand should produce a null result.

Current implementation behavior:

- `LiteralValue` includes `NullType`.
- `ArrowUtils::make_array_from_literals(...)` writes nulls into Arrow arrays.
- `Column::value_at(...)` returns `NullType` for null slots.
- arithmetic, comparison, boolean, and string operations in `Column.cpp` propagate null row-wise.
- aggregate execution in the lazy executor skips nulls for `count` and numeric summaries, and returns null when a group has no valid inputs.

This is one of the strongest parts of the current codebase and is already reflected consistently across the column and lazy-expression execution paths.

## 3. Numeric Promotion Rules

The assignment explicitly calls out numeric promotion such as `int + float = float`.

Current implementation:

- promotion logic lives in `src/Types.cpp`
- `int32 + int64 -> int64`
- `int32 + float32 -> float32`
- `int32 + float64 -> float64`
- `float32 + float64 -> float64`
- `int64 + float32 -> float64`

That last rule is a safe widening choice and avoids silent narrowing.

## 4. Expression System Pipeline

The assignment requires symbolic expressions for:

- core expressions: `col`, `lit`, aliasing
- arithmetic
- comparison and boolean logic
- string functions
- aggregation functions

Current design pipeline:

1. Build an `Expression` tree instead of executing immediately.
2. Store node kind, operator kind, optional payload, and child expressions.
3. Use `infer_type(schema)` to validate the expression against an input schema.
4. Use `required_columns()` for optimizer planning and projection analysis.
5. Evaluate expressions later inside an execution engine.

Supported expression families in the current code:

- column references
- literals, including `nullptr`
- aliases
- `+ - * / %`
- `== != < <= > >=`
- `& | ~`
- `is_null`
- `is_not_null`
- `abs`
- `length`
- `contains`
- `starts_with`
- `ends_with`
- `to_lower`
- `to_upper`
- `sum`
- `mean`
- `count`
- `min`
- `max`

## 5. API Shape And Assignment Alignment

The strict requirements examples use member-style expression chaining such as:

- `col("x").alias("y")`
- `col("x").abs()`
- `col("city").to_lower()`
- `col("salary").mean()`

The original code exposed only free-function helpers such as `alias(expr, name)` and `mean(expr)`.

To reduce that mismatch, the `Expression` class now also exposes member convenience wrappers for:

- aliasing
- unary numeric/string/null helpers
- string predicates
- aggregation helpers

The free-function API still exists, so both styles are now supported.

## 6. One Important Architectural Reality

The expression system itself is implemented and type-safe, but eager execution still does not evaluate arbitrary expressions inside `src/Eager.cpp`.

Right now:

- `Expression::infer_type(...)` is implemented,
- expression construction is implemented,
- lazy execution evaluates expressions in `src/Lazy.cpp`,
- eager `select(expressions)`, `filter`, and `with_column` are still stubbed.

So the symbolic layer is ready, but the eager execution layer is not yet fully wired to it.

## 7. Why This Group Matters For Step 5

The optimizer relies heavily on this subsystem:

- constant folding needs literal-aware expression rewriting,
- simplification needs operator identities,
- predicate pushdown needs `required_columns()`,
- projection pushdown needs exact column dependencies,
- validation after rewrites needs `infer_type(...)`.

So even though Step 5 is still unimplemented, the type-and-expression foundation it depends on is largely in place.
