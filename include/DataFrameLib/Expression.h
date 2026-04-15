#include<vector>
#include<string>
#include "Types.h"
#include "arrow/api.h"

// ===============================
// include/DataFrameLib/Expression.h
// ===============================

// Purpose:
// A small but important layer that describes computations symbolically.
// In Step 1, you do not need the full lazy DAG yet.
// You only need enough expression structure to represent typed column
// operations cleanly and safely.

// Main goals of this file:
// - Build expressions like col("x") + lit(2)
// - Check types before execution
// - Preserve null semantics at the expression level
// - Keep the later Eager and Lazy layers simple

// Recommended structure:

enum class ExprKind {
    ColumnRef,
    Literal,
    Alias,
    UnaryOp,
    BinaryOp,
    ComparisonOp,
    BooleanOp,
    StringOp,
    AggregateOp,
    IsNull,
    IsNotNull
};

enum class OpKind {
    Add, Subtract, Multiply, Divide, Modulo,
    Equal, NotEqual, LessThan, LessEqual, GreaterThan, GreaterEqual,
    And, Or, Not,
    Abs, Length, Contains, StartsWith, EndsWith, ToLower, ToUpper,
    Sum, Mean, Count, Min, Max
};

// the expression class should behave like a tiny syntax tree node. 
// It does not compute immediately; it only remembers the structure 
// so later you can validate types and evaluate or optimize it.
class Expression {
public:
    // Constructors are usually hidden.
    // Use factory helpers such as col(...), lit(...), etc.

    DataType infer_type(const arrow::Schema& schema) const;
    // Checks the expression against an input schema and determines the
    // resulting output type.
    // Must throw immediately if the expression is not valid.

    std::vector<std::string> required_columns() const;
    // Returns the input column names needed to evaluate the expression.

    bool is_constant() const;
    // True only if the expression depends on no input column.

    std::string alias_name() const;
    // Returns the alias if one is attached, otherwise an empty string.

    Expression with_alias(const std::string& name) const;
    // Returns a new expression that produces the same value with a new name.

private:
    // Internal tree storage.
    ExprKind kind_;
    OpKind op_;
    std::optional<std::string> name_;
    std::optional<LiteralValue> literal_;
    std::vector<std::shared_ptr<Expression>> children_;
    std::optional<DataType> cached_type_;
};

// Factory helpers that should exist:

Expression col(const std::string& name);
//   Represents a reference to an existing column.

Expression lit(const LiteralValue& value);
//   Represents a typed scalar literal.

Expression lit(std::nullptr_t);
//   Convenience form for a null literal.
//   The eventual result type must still be inferred from context.

Expression alias(const Expression& expr, const std::string& name);
//   Renames the expression output without changing its semantics.

Expression abs(const Expression& expr);
Expression is_null(const Expression& expr);
Expression is_not_null(const Expression& expr);
Expression length(const Expression& expr);
Expression to_lower(const Expression& expr);
Expression to_upper(const Expression& expr);

Expression contains(const Expression& expr, const std::string& needle);
Expression starts_with(const Expression& expr, const std::string& prefix);
Expression ends_with(const Expression& expr, const std::string& suffix);

Expression sum(const Expression& expr);
Expression mean(const Expression& expr);
Expression count(const Expression& expr);
Expression min(const Expression& expr);
Expression max(const Expression& expr);

// Operator helpers for expressions:

Expression operator+(const Expression& lhs, const Expression& rhs);
Expression operator-(const Expression& lhs, const Expression& rhs);
Expression operator*(const Expression& lhs, const Expression& rhs);
Expression operator/(const Expression& lhs, const Expression& rhs);
Expression operator%(const Expression& lhs, const Expression& rhs);

Expression operator==(const Expression& lhs, const Expression& rhs);
Expression operator!=(const Expression& lhs, const Expression& rhs);
Expression operator<(const Expression& lhs, const Expression& rhs);
Expression operator<=(const Expression& lhs, const Expression& rhs);
Expression operator>(const Expression& lhs, const Expression& rhs);
Expression operator>=(const Expression& lhs, const Expression& rhs);

Expression operator&(const Expression& lhs, const Expression& rhs);
Expression operator|(const Expression& lhs, const Expression& rhs);
Expression operator~(const Expression& expr);

// Detailed behavior expected:

// infer_type(schema)
//   - Column references resolve against the schema.
//   - Literals resolve to their exact type.
//   - Arithmetic expressions apply promotion rules.
//   - String functions validate that the input is string.
//   - Boolean operators validate that inputs are boolean.
//   - Aggregations validate input type and define an output type.
//   - Invalid combinations throw TypeError immediately.
//
// required_columns()
//   - Used later by optimizers and execution planning.
//   - For a literal, this should be empty.
//   - For col("x"), it should contain only "x".
//
// is_constant()
//   - Returns true only for pure literal expressions or expressions fully
//     reducible at build time.
//
// with_alias(name)
//   - Useful for named output columns in select and with_column-like operations.
//   - Must preserve the underlying expression tree.
