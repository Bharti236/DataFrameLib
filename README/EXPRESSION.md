```cpp
// ===============================
// Expression internal storage
// ===============================
//
// The Expression class should store a symbolic computation tree.
// Each Expression object represents exactly one node in that tree.
//
// The goal of this storage is:
// - to remember what kind of expression this is,
// - to remember its child expressions,
// - to remember any extra data needed by that node,
// - and to allow later type-checking, optimization, and evaluation.
//
// A good internal representation usually needs:
//
// 1) A node kind
//    This tells what category the expression belongs to.
//    Examples:
//    - ColumnRef
//    - Literal
//    - Alias
//    - UnaryOp
//    - BinaryOp
//    - ComparisonOp
//    - BooleanOp
//    - StringOp
//    - AggregateOp
//
// 2) Operator / function metadata
//    For nodes like +, -, ==, &, contains, sum, etc., the class must store
//    which exact operation it represents.
//    This is often done using an enum.
//
// 3) Child expressions
//    Most expressions are built from smaller expressions.
//    Example:
//      col("a") + lit(5)
//    has two children:
//      left  -> col("a")
//      right -> lit(5)
//    Unary expressions like abs(col("x")) have one child.
//    Literal and column-reference nodes usually have no children.
//
// 4) Optional payload data
//    Some nodes need extra information:
//    - ColumnRef needs a column name
//    - Literal needs a literal value
//    - Alias needs the output name
//    - String functions need a substring / prefix / suffix argument
//
// 5) Optional cached type information
//    You may optionally store an inferred result type after validation.
//    This is not mandatory, but it can avoid recomputing types repeatedly.
//
// Suggested private members:
//
// enum class ExprKind {
//     ColumnRef,
//     Literal,
//     Alias,
//     UnaryOp,
//     BinaryOp,
//     ComparisonOp,
//     BooleanOp,
//     StringOp,
//     AggregateOp,
//     IsNull,
//     IsNotNull
// };
//
// enum class OpKind {
//     Add, Subtract, Multiply, Divide, Modulo,
//     Equal, NotEqual, LessThan, LessEqual, GreaterThan, GreaterEqual,
//     And, Or, Not,
//     Abs, Length, Contains, StartsWith, EndsWith, ToLower, ToUpper,
//     Sum, Mean, Count, Min, Max
// };
//
// class Expression {
// private:
//     ExprKind kind_;
//     OpKind op_;
//     std::optional<std::string> name_;
//     std::optional<LiteralValue> literal_;
//     std::vector<std::shared_ptr<Expression>> children_;
//     std::optional<DataType> cached_type_;
// };
//
// Member meaning:
//
// kind_
//   Tells what kind of node this is.
//
// op_
//   Tells which exact operator or function the node represents.
//   This is only meaningful for operator-based nodes.
//
// name_
//   Stores a column name or alias name when the node needs one.
//   Example: col("age") stores "age".
//   Example: alias(..., "salary_usd") stores "salary_usd".
//
// literal_
//   Stores a literal value for lit(...).
//   It should be empty for all non-literal nodes.
//
// children_
//   Stores the expression tree edges.
//   The number of children depends on the node type:
//   - 0 for literals and column refs
//   - 1 for unary ops
//   - 2 for binary ops
//   - sometimes more for future extensions
//
// cached_type_
//   Optional memoized result type.
//   Useful after infer_type(schema) succeeds.
//   If you use this, make sure it never overrides the actual rules.
//   It is only a cache, not the source of truth.
//
// Important design rules:
//
// - Expression objects should be immutable after construction.
// - Any transformation like aliasing should return a new Expression.
// - Do not mutate child expressions in place.
// - Do not store actual DataFrame data here.
// - Do not evaluate immediately here.
// - This class is only for representing the computation structure.
//
// Example shape:
//
// Expression for (col("a") + lit(5)) could look like:
//
// kind_      = BinaryOp
// op_        = Add
// children_   = [ Expression(col("a")), Expression(lit(5)) ]
// literal_    = empty
// name_       = empty
//
// Expression for alias(col("x"), "y") could look like:
//
// kind_      = Alias
// name_      = "y"
// children_   = [ Expression(col("x")) ]
//
// Expression for lit(100) could look like:
//
// kind_      = Literal
// literal_   = 100
// children_  = empty
//
// This structure is what allows the rest of the library to:
// - infer types,
// - validate operations early,
// - optimize plans later,
// - and finally evaluate the expression on Arrow columns.
```