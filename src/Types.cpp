#include "Types.h"

#include <stdexcept>
#include <utility>

namespace {

[[noreturn]] void type_error(const std::string& message) {
    throw std::invalid_argument("DataType error: " + message);
}

bool is_any_numeric(DataType t) {
    return t == DataType::Int32 ||
           t == DataType::Int64 ||
           t == DataType::Float32 ||
           t == DataType::Float64;
}

} // namespace

bool is_numeric(DataType t) {
    return is_any_numeric(t);
}

bool is_integral(DataType t) {
    return t == DataType::Int32 || t == DataType::Int64;
}

bool is_floating(DataType t) {
    return t == DataType::Float32 || t == DataType::Float64;
}

bool is_string(DataType t) {
    return t == DataType::String;
}

bool is_boolean(DataType t) {
    return t == DataType::Boolean;
}

bool is_nullable_type(DataType) {
    // Every supported type may contain Arrow nulls.
    return true;
}

// A small design note: I chose int64 + float32 -> float64 so it never narrows values silently. That keeps the rules safe and consistent.

std::string type_name(DataType t) {
    switch (t) {
        case DataType::Int32:   return "int32";
        case DataType::Int64:   return "int64";
        case DataType::Float32: return "float32";
        case DataType::Float64: return "float64";
        case DataType::String:  return "string";
        case DataType::Boolean: return "boolean";
        default:
            type_error("unknown DataType value");
    }
}

static DataType promote_numeric_pair(DataType left, DataType right) {
    if (!is_numeric(left) || !is_numeric(right)) {
        type_error("numeric promotion requested for non-numeric types: " +
                   type_name(left) + ", " + type_name(right));
    }

    if (left == right) {
        return left;
    }

    // Any float64 involvement promotes to float64.
    if (left == DataType::Float64 || right == DataType::Float64) {
        return DataType::Float64;
    }

    // int64 + float32 cannot be safely narrowed to float32.
    if ((left == DataType::Int64 && right == DataType::Float32) ||
        (left == DataType::Float32 && right == DataType::Int64)) {
        return DataType::Float64;
    }

    // int32 + int64 -> int64
    if ((left == DataType::Int32 && right == DataType::Int64) ||
        (left == DataType::Int64 && right == DataType::Int32)) {
        return DataType::Int64;
    }

    // int32 + float32 -> float32
    if ((left == DataType::Int32 && right == DataType::Float32) ||
        (left == DataType::Float32 && right == DataType::Int32)) {
        return DataType::Float32;
    }

    // int32 + float64 -> float64
    if ((left == DataType::Int32 && right == DataType::Float64) ||
        (left == DataType::Float64 && right == DataType::Int32)) {
        return DataType::Float64;
    }

    // Fallback for any future numeric combination.
    return DataType::Float64;
}

DataType promote_numeric(DataType left, DataType right) {
    return promote_numeric_pair(left, right);
}

DataType promote_for_comparison(DataType left, DataType right) {
    if (is_numeric(left) && is_numeric(right)) {
        return promote_numeric_pair(left, right);
    }

    if (left == DataType::String && right == DataType::String) {
        return DataType::String;
    }

    if (left == DataType::Boolean && right == DataType::Boolean) {
        return DataType::Boolean;
    }

    type_error("incompatible comparison between " + type_name(left) +
               " and " + type_name(right));
}

bool can_add(DataType left, DataType right) {
    return is_numeric(left) && is_numeric(right);
}

bool can_subtract(DataType left, DataType right) {
    return is_numeric(left) && is_numeric(right);
}

bool can_multiply(DataType left, DataType right) {
    return is_numeric(left) && is_numeric(right);
}

bool can_divide(DataType left, DataType right) {
    return is_numeric(left) && is_numeric(right);
}

bool can_modulo(DataType left, DataType right) {
    return is_integral(left) && is_integral(right);
}

bool can_compare(DataType left, DataType right) {
    if (is_numeric(left) && is_numeric(right)) {
        return true;
    }

    if (left == DataType::String && right == DataType::String) {
        return true;
    }

    if (left == DataType::Boolean && right == DataType::Boolean) {
        return true;
    }

    return false;
}

bool can_apply_boolean_op(DataType left, DataType right) {
    return left == DataType::Boolean && right == DataType::Boolean;
}

bool can_apply_string_op(DataType t) {
    return t == DataType::String;
}

DataType result_type_for_unary_abs(DataType input) {
    if (is_integral(input) || is_floating(input)) {
        return input;
    }

    type_error("abs() requires a numeric input, got " + type_name(input));
}

DataType result_type_for_length(DataType input) {
    if (input == DataType::String) {
        return DataType::Int32;
    }

    type_error("length() requires a string input, got " + type_name(input));
}