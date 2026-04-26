# Expression Notes

Relevant files:

- `include/DataFrameLib/Expression.h`
- `src/Expression.cpp`

## Purpose

`Expression` stores symbolic computations that can be:

- type-checked against a schema
- inspected by the optimizer
- evaluated later by the execution engine

## Core Structure

Each expression stores:

- an `ExprKind`
- an `OpKind`
- optional name payload
- optional literal payload
- child expressions

This is enough to represent:

- column references
- literals
- aliases
- unary operations
- binary operations
- comparisons
- boolean logic
- string helpers
- aggregate expressions

## Construction Styles

The public API supports both styles below:

```cpp
auto a = col("salary") * 2.0;
auto b = alias(col("salary") * lit(LiteralValue{2.0}), "salary_doubled");
```

Literal-friendly operator shims are in the header so you can write normal-looking expressions without spelling `lit(...)` every time.

## Output Naming

Aliases are preserved through execution and optimization.

When a projection expression has no explicit alias, the executor derives a stable fallback output name such as:

- the source column name for `col("x")`
- `literal_N` for bare literals
- `expr_N` for computed expressions

Those derived names are also used by plan validation and optimizer bookkeeping.
