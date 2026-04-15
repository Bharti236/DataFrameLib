# Standalone lit(nullptr) will not infer a type by itself

`lit(nullptr)` represents a **null value**, but in your design it does **not** carry an actual data type by itself.

A null can stand in for any of your supported types: `int32`, `float64`, `string`, `boolean`, and so on. So just seeing `null` is not enough to know whether the expression is supposed to be an integer null, a string null, or a boolean null. That is why `infer_type()` cannot decide a type from `lit(nullptr)` alone.

For example:

```cpp
lit(nullptr) + lit(5)
```

Here the `5` tells you the right side is numeric, so the null can be treated as a numeric null in that context.

But this:

```cpp
lit(nullptr)
```

has no surrounding expression, so there is no way to know what its result type should be. In that case, your code should throw or delay type inference until the null is used inside something that supplies context.

So the rule is:

`null` has a value, but not an intrinsic type; the type comes from context.

It should **work for this project**. The spec says the library supports only `int32`, `int64`, `float32`, `float64`, `string`, and `boolean`, while missing values are represented as `null` only, not as a supported column type. It also says any operation involving a null operand must produce a null result. 

So a bare `lit(nullptr)` is best treated as an **untyped null placeholder**, not as a value with its own intrinsic type. That is consistent with the assignment, because the required type system has no `Null` type, and the expression system only needs `lit(value)` as a core expression constructor. 

The only caveat is practical: `infer_type(lit(nullptr))` by itself cannot return one of the six allowed types without extra context. That is not a project problem unless your own tests or API usage specifically expect standalone null literals to be typed. For the assignment spec, context-sensitive null handling is the safer design. 
