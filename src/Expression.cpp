// Two important notes:
// required_columns() and is_constant() 
// are implemented recursively and do not need the cache field.
// lit(nullptr) is supported, 
// but a standalone null literal cannot infer a type by itself; 
// it only becomes type-safe when used inside a context that supplies the type.

#include "../include/DataFrameLib/Expression.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>

#include "../include/DataFrameLib/Errors.h"
#include "../include/DataFrameLib/Types.h"

namespace {

DataType literal_type(const LiteralValue& v) {
    return std::visit([](const auto& x) -> DataType {
        using T = std::decay_t<decltype(x)>;

        if constexpr (std::is_same_v<T, NullType>) {
            throw TypeError("cannot infer type of null literal without context");
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return DataType::Int32;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return DataType::Int64;
        } else if constexpr (std::is_same_v<T, float>) {
            return DataType::Float32;
        } else if constexpr (std::is_same_v<T, double>) {
            return DataType::Float64;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return DataType::String;
        } else if constexpr (std::is_same_v<T, bool>) {
            return DataType::Boolean;
        } else {
            static_assert(std::is_same_v<T, void>, "Unhandled LiteralValue alternative");
        }
    }, v);
}

DataType arrow_type_to_data_type(const std::shared_ptr<arrow::DataType>& t) {
    switch (t->id()) {
        case arrow::Type::INT32:  return DataType::Int32;
        case arrow::Type::INT64:  return DataType::Int64;
        case arrow::Type::FLOAT:  return DataType::Float32;
        case arrow::Type::DOUBLE: return DataType::Float64;
        case arrow::Type::STRING: return DataType::String;
        case arrow::Type::BOOL:   return DataType::Boolean;
        default:
            throw TypeError("unsupported Arrow type: " + t->ToString());
    }
}

} // namespace

Expression::Expression(ExprKind kind,
                       OpKind op,
                       std::optional<std::string> name,
                       std::optional<LiteralValue> literal,
                       std::vector<std::shared_ptr<Expression>> children)
    : kind_(kind),
      op_(op),
      name_(std::move(name)),
      literal_(std::move(literal)),
      children_(std::move(children)),
      cached_type_(std::nullopt) {}

DataType Expression::infer_type(const arrow::Schema& schema) const {
    switch (kind_) {
        case ExprKind::ColumnRef: {
            if (!name_) {
                throw SchemaError("column reference has no name");
            }

            auto field = schema.GetFieldByName(*name_);
            if (!field) {
                throw ColumnNotFoundError("column not found: " + *name_);
            }

            return arrow_type_to_data_type(field->type());
        }

        case ExprKind::Literal: {
            if (!literal_) {
                throw TypeError("invalid literal expression");
            }
            return literal_type(*literal_);
        }

        case ExprKind::Alias: {
            if (children_.size() != 1 || !children_[0]) {
                throw TypeError("invalid alias expression");
            }
            return children_[0]->infer_type(schema);
        }

        case ExprKind::IsNull:
        case ExprKind::IsNotNull:
            if (children_.size() != 1 || !children_[0]) {
                throw TypeError("invalid null-check expression");
            }
            return DataType::Boolean;

        case ExprKind::UnaryOp: {
            if (children_.size() != 1 || !children_[0]) {
                throw TypeError("invalid unary expression");
            }

            const Expression& child = *children_[0];
            const bool child_is_null =
                child.kind_ == ExprKind::Literal &&
                child.literal_.has_value() &&
                std::holds_alternative<NullType>(*child.literal_);

            if (op_ == OpKind::Not) {
                if (child_is_null) {
                    return DataType::Boolean;
                }
                DataType t = child.infer_type(schema);
                if (!is_boolean(t)) {
                    throw_unary_type_error("not", t);
                }
                return DataType::Boolean;
            }

            if (op_ == OpKind::Abs) {
                if (child_is_null) {
                    throw TypeError("abs(null) cannot infer a type without context");
                }
                DataType t = child.infer_type(schema);
                if (!is_numeric(t)) {
                    throw_unary_type_error("abs", t);
                }
                return result_type_for_unary_abs(t);
            }

            if (op_ == OpKind::Length) {
                if (child_is_null) {
                    throw TypeError("length(null) cannot infer a type without context");
                }
                DataType t = child.infer_type(schema);
                if (!is_string(t)) {
                    throw_unary_type_error("length", t);
                }
                return result_type_for_length(t);
            }

            if (op_ == OpKind::ToLower) {
                if (child_is_null) {
                    throw TypeError("to_lower(null) cannot infer a type without context");
                }
                DataType t = child.infer_type(schema);
                if (!is_string(t)) {
                    throw_unary_type_error("to_lower", t);
                }
                return DataType::String;
            }

            if (op_ == OpKind::ToUpper) {
                if (child_is_null) {
                    throw TypeError("to_upper(null) cannot infer a type without context");
                }
                DataType t = child.infer_type(schema);
                if (!is_string(t)) {
                    throw_unary_type_error("to_upper", t);
                }
                return DataType::String;
            }

            throw TypeError("unsupported unary operator");
        }

        case ExprKind::StringOp: {
            if (op_ == OpKind::Contains ||
                op_ == OpKind::StartsWith ||
                op_ == OpKind::EndsWith) {

                if (children_.size() != 2 || !children_[0] || !children_[1]) {
                    throw TypeError("invalid string predicate expression");
                }

                const Expression& left = *children_[0];
                const Expression& right = *children_[1];

                const bool left_is_null =
                    left.kind_ == ExprKind::Literal &&
                    left.literal_.has_value() &&
                    std::holds_alternative<NullType>(*left.literal_);

                const bool right_is_null =
                    right.kind_ == ExprKind::Literal &&
                    right.literal_.has_value() &&
                    std::holds_alternative<NullType>(*right.literal_);

                if (!left_is_null) {
                    DataType lt = left.infer_type(schema);
                    if (!is_string(lt)) {
                        throw_unary_type_error("string operation", lt);
                    }
                }

                if (!right_is_null) {
                    DataType rt = right.infer_type(schema);
                    if (!is_string(rt)) {
                        throw_unary_type_error("string operation", rt);
                    }
                }

                return DataType::Boolean;
            }

            if (children_.size() != 1 || !children_[0]) {
                throw TypeError("invalid string expression");
            }

            const Expression& child = *children_[0];
            const bool child_is_null =
                child.kind_ == ExprKind::Literal &&
                child.literal_.has_value() &&
                std::holds_alternative<NullType>(*child.literal_);

            if (child_is_null) {
                throw TypeError("string operation on null cannot infer a type without context");
            }

            DataType t = child.infer_type(schema);

            if (op_ == OpKind::Length) {
                if (!is_string(t)) throw_unary_type_error("length", t);
                return DataType::Int32;
            }
            if (op_ == OpKind::ToLower) {
                if (!is_string(t)) throw_unary_type_error("to_lower", t);
                return DataType::String;
            }
            if (op_ == OpKind::ToUpper) {
                if (!is_string(t)) throw_unary_type_error("to_upper", t);
                return DataType::String;
            }

            throw TypeError("unsupported string operator");
        }

        case ExprKind::BinaryOp: {
            if (children_.size() != 2 || !children_[0] || !children_[1]) {
                throw TypeError("invalid binary expression");
            }

            const Expression& lhs = *children_[0];
            const Expression& rhs = *children_[1];

            const bool lhs_is_null =
                lhs.kind_ == ExprKind::Literal &&
                lhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*lhs.literal_);

            const bool rhs_is_null =
                rhs.kind_ == ExprKind::Literal &&
                rhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*rhs.literal_);

            if (op_ == OpKind::Add ||
                op_ == OpKind::Subtract ||
                op_ == OpKind::Multiply) {

                if (lhs_is_null && rhs_is_null) {
                    throw TypeError("binary operation on two null literals cannot infer a type");
                }

                if (lhs_is_null) {
                    DataType rt = rhs.infer_type(schema);
                    if (!is_numeric(rt)) throw_type_error("+", DataType::Int32, rt);
                    return rt;
                }

                if (rhs_is_null) {
                    DataType lt = lhs.infer_type(schema);
                    if (!is_numeric(lt)) throw_type_error("+", lt, DataType::Int32);
                    return lt;
                }

                DataType lt = lhs.infer_type(schema);
                DataType rt = rhs.infer_type(schema);

                switch (op_) {
                    case OpKind::Add:
                        if (!can_add(lt, rt)) throw_type_error("+", lt, rt);
                        return promote_numeric(lt, rt);
                    case OpKind::Subtract:
                        if (!can_subtract(lt, rt)) throw_type_error("-", lt, rt);
                        return promote_numeric(lt, rt);
                    case OpKind::Multiply:
                        if (!can_multiply(lt, rt)) throw_type_error("*", lt, rt);
                        return promote_numeric(lt, rt);
                    default:
                        break;
                }
            }

            if (op_ == OpKind::Divide) {
                if (lhs_is_null || rhs_is_null) {
                    return DataType::Float64;
                }
                DataType lt = lhs.infer_type(schema);
                DataType rt = rhs.infer_type(schema);
                if (!is_numeric(lt) || !is_numeric(rt)) {
                    throw_type_error("/", lt, rt);
                }
                return DataType::Float64;
            }

            if (op_ == OpKind::Modulo) {
                if (lhs_is_null && rhs_is_null) {
                    throw TypeError("modulo of two null literals cannot infer a type");
                }

                if (lhs_is_null) {
                    DataType rt = rhs.infer_type(schema);
                    if (!is_integral(rt)) throw_type_error("%", DataType::Int32, rt);
                    return rt;
                }

                if (rhs_is_null) {
                    DataType lt = lhs.infer_type(schema);
                    if (!is_integral(lt)) throw_type_error("%", lt, DataType::Int32);
                    return lt;
                }

                DataType lt = lhs.infer_type(schema);
                DataType rt = rhs.infer_type(schema);
                if (!can_modulo(lt, rt)) throw_type_error("%", lt, rt);
                return promote_numeric(lt, rt);
            }

            throw TypeError("unsupported binary operator");
        }

        case ExprKind::ComparisonOp: {
            if (children_.size() != 2 || !children_[0] || !children_[1]) {
                throw TypeError("invalid comparison expression");
            }

            const Expression& lhs = *children_[0];
            const Expression& rhs = *children_[1];

            const bool lhs_is_null =
                lhs.kind_ == ExprKind::Literal &&
                lhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*lhs.literal_);

            const bool rhs_is_null =
                rhs.kind_ == ExprKind::Literal &&
                rhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*rhs.literal_);

            if (!lhs_is_null && !rhs_is_null) {
                DataType lt = lhs.infer_type(schema);
                DataType rt = rhs.infer_type(schema);
                if (!can_compare(lt, rt)) {
                    throw_type_error("comparison", lt, rt);
                }
                (void)promote_for_comparison(lt, rt);
            }

            return DataType::Boolean;
        }

        case ExprKind::BooleanOp: {
            if (children_.size() != 2 || !children_[0] || !children_[1]) {
                throw TypeError("invalid boolean expression");
            }

            const Expression& lhs = *children_[0];
            const Expression& rhs = *children_[1];

            const bool lhs_is_null =
                lhs.kind_ == ExprKind::Literal &&
                lhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*lhs.literal_);

            const bool rhs_is_null =
                rhs.kind_ == ExprKind::Literal &&
                rhs.literal_.has_value() &&
                std::holds_alternative<NullType>(*rhs.literal_);

            if (!lhs_is_null && !rhs_is_null) {
                DataType lt = lhs.infer_type(schema);
                DataType rt = rhs.infer_type(schema);
                if (!can_apply_boolean_op(lt, rt)) {
                    throw_type_error("boolean op", lt, rt);
                }
            }

            return DataType::Boolean;
        }

        case ExprKind::AggregateOp: {
            if (children_.size() != 1 || !children_[0]) {
                throw TypeError("invalid aggregate expression");
            }

            const Expression& child = *children_[0];
            const bool child_is_null =
                child.kind_ == ExprKind::Literal &&
                child.literal_.has_value() &&
                std::holds_alternative<NullType>(*child.literal_);

            switch (op_) {
                case OpKind::Count:
                    return DataType::Int64;

                case OpKind::Sum: {
                    if (child_is_null) {
                        throw TypeError("sum(null) cannot infer a type without context");
                    }
                    DataType t = child.infer_type(schema);
                    if (!is_numeric(t)) {
                        throw TypeError("sum() requires a numeric input, got " + type_name(t));
                    }
                    return (t == DataType::Int32 || t == DataType::Int64)
                        ? DataType::Int64
                        : t;
                }

                case OpKind::Mean: {
                    if (child_is_null) {
                        throw TypeError("mean(null) cannot infer a type without context");
                    }
                    DataType t = child.infer_type(schema);
                    if (!is_numeric(t)) {
                        throw TypeError("mean() requires a numeric input, got " + type_name(t));
                    }
                    return DataType::Float64;
                }

                case OpKind::Min:
                case OpKind::Max: {
                    if (child_is_null) {
                        throw TypeError("min/max on null cannot infer a type without context");
                    }
                    return child.infer_type(schema);
                }

                default:
                    throw TypeError("unsupported aggregate operation");
            }
        }
    }

    throw TypeError("unreachable expression kind");
}

std::vector<std::string> Expression::required_columns() const {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;

    std::function<void(const Expression&)> visit = [&](const Expression& e) {
        if (e.kind_ == ExprKind::ColumnRef) {
            if (e.name_ && seen.insert(*e.name_).second) {
                out.push_back(*e.name_);
            }
            return;
        }

        for (const auto& ch : e.children_) {
            if (ch) visit(*ch);
        }
    };

    visit(*this);
    return out;
}

bool Expression::is_constant() const {
    std::function<bool(const Expression&)> visit = [&](const Expression& e) -> bool {
        if (e.kind_ == ExprKind::ColumnRef) return false;
        if (e.kind_ == ExprKind::Literal) return true;

        for (const auto& ch : e.children_) {
            if (ch && !visit(*ch)) return false;
        }
        return true;
    };

    return visit(*this);
}

std::string Expression::alias_name() const {
    if (kind_ == ExprKind::Alias && name_) {
        return *name_;
    }
    return "";
}

Expression Expression::with_alias(const std::string& name) const {
    return ::alias(*this, name);
}

Expression Expression::alias(const std::string& name) const {
    return ::alias(*this, name);
}

Expression Expression::abs() const {
    return ::abs(*this);
}

Expression Expression::is_null() const {
    return ::is_null(*this);
}

Expression Expression::is_not_null() const {
    return ::is_not_null(*this);
}

Expression Expression::length() const {
    return ::length(*this);
}

Expression Expression::contains(const std::string& needle) const {
    return ::contains(*this, needle);
}

Expression Expression::starts_with(const std::string& prefix) const {
    return ::starts_with(*this, prefix);
}

Expression Expression::ends_with(const std::string& suffix) const {
    return ::ends_with(*this, suffix);
}

Expression Expression::to_lower() const {
    return ::to_lower(*this);
}

Expression Expression::to_upper() const {
    return ::to_upper(*this);
}

Expression Expression::sum() const {
    return ::sum(*this);
}

Expression Expression::mean() const {
    return ::mean(*this);
}

Expression Expression::count() const {
    return ::count(*this);
}

Expression Expression::min() const {
    return ::min(*this);
}

Expression Expression::max() const {
    return ::max(*this);
}

// -------------------------
// factories
// -------------------------

Expression col(const std::string& name) {
    return Expression(ExprKind::ColumnRef, OpKind::Add, name, std::nullopt, {});
}

Expression lit(const LiteralValue& value) {
    return Expression(ExprKind::Literal, OpKind::Add, std::nullopt, value, {});
}

Expression lit(std::nullptr_t) {
    return Expression(ExprKind::Literal, OpKind::Add, std::nullopt, LiteralValue{NullType{}}, {});
}

Expression alias(const Expression& expr, const std::string& name) {
    return Expression(
        ExprKind::Alias,
        OpKind::Add,
        name,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression abs(const Expression& expr) {
    return Expression(
        ExprKind::UnaryOp,
        OpKind::Abs,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression is_null(const Expression& expr) {
    return Expression(
        ExprKind::IsNull,
        OpKind::Not,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression is_not_null(const Expression& expr) {
    return Expression(
        ExprKind::IsNotNull,
        OpKind::Not,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression length(const Expression& expr) {
    return Expression(
        ExprKind::StringOp,
        OpKind::Length,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression to_lower(const Expression& expr) {
    return Expression(
        ExprKind::StringOp,
        OpKind::ToLower,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression to_upper(const Expression& expr) {
    return Expression(
        ExprKind::StringOp,
        OpKind::ToUpper,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression contains(const Expression& expr, const std::string& needle) {
    return Expression(
        ExprKind::StringOp,
        OpKind::Contains,
        std::nullopt,
        std::nullopt,
        {
            std::make_shared<Expression>(expr),
            std::make_shared<Expression>(lit(LiteralValue{needle}))
        }
    );
}

Expression starts_with(const Expression& expr, const std::string& prefix) {
    return Expression(
        ExprKind::StringOp,
        OpKind::StartsWith,
        std::nullopt,
        std::nullopt,
        {
            std::make_shared<Expression>(expr),
            std::make_shared<Expression>(lit(LiteralValue{prefix}))
        }
    );
}

Expression ends_with(const Expression& expr, const std::string& suffix) {
    return Expression(
        ExprKind::StringOp,
        OpKind::EndsWith,
        std::nullopt,
        std::nullopt,
        {
            std::make_shared<Expression>(expr),
            std::make_shared<Expression>(lit(LiteralValue{suffix}))
        }
    );
}

Expression sum(const Expression& expr) {
    return Expression(
        ExprKind::AggregateOp,
        OpKind::Sum,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression mean(const Expression& expr) {
    return Expression(
        ExprKind::AggregateOp,
        OpKind::Mean,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression count(const Expression& expr) {
    return Expression(
        ExprKind::AggregateOp,
        OpKind::Count,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression min(const Expression& expr) {
    return Expression(
        ExprKind::AggregateOp,
        OpKind::Min,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

Expression max(const Expression& expr) {
    return Expression(
        ExprKind::AggregateOp,
        OpKind::Max,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}

// -------------------------
// operators
// -------------------------

Expression operator+(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BinaryOp,
        OpKind::Add,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator-(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BinaryOp,
        OpKind::Subtract,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator*(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BinaryOp,
        OpKind::Multiply,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator/(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BinaryOp,
        OpKind::Divide,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator%(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BinaryOp,
        OpKind::Modulo,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator==(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::Equal,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator!=(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::NotEqual,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator<(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::LessThan,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator<=(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::LessEqual,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator>(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::GreaterThan,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator>=(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::ComparisonOp,
        OpKind::GreaterEqual,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator&(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BooleanOp,
        OpKind::And,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator|(const Expression& lhs, const Expression& rhs) {
    return Expression(
        ExprKind::BooleanOp,
        OpKind::Or,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(lhs), std::make_shared<Expression>(rhs)}
    );
}

Expression operator~(const Expression& expr) {
    return Expression(
        ExprKind::UnaryOp,
        OpKind::Not,
        std::nullopt,
        std::nullopt,
        {std::make_shared<Expression>(expr)}
    );
}
