// Strict type definitions (int32, string, etc.)

#include <string>
#include <variant>

// ===============================
// include/DataFrameLib/Types.h
// ===============================

// Purpose:
// This file defines the strict logical type system for the library.
// It is the single source of truth for all supported scalar/column types,
// promotion rules, and type-validation helpers.
//
// The assignment requires the library to support only these immutable types:
// int32, int64, float32, float64, string, and boolean.
// Missing values must be represented as Arrow nulls only, not NaN.

// Notes:
// - No DataFrame logic should live here.
// - No Arrow execution logic should live here.
// - This file should be small, stable, and widely reused by the rest of the code.

// Recommended declarations:

enum class DataType {
    Int32,
    Int64,
    Float32,
    Float64,
    String,
    Boolean
};

using NullType = std::monostate;

using LiteralValue = std::variant<NullType, int32_t, int64_t, float, double, std::string, bool>;

// Functions that should exist:

bool is_numeric(DataType t);
//   Returns true only for Int32, Int64, Float32, Float64.

bool is_integral(DataType t);
//   Returns true only for Int32 and Int64.

bool is_floating(DataType t);
//   Returns true only for Float32 and Float64.

bool is_string(DataType t);
//   Returns true only for String.

bool is_boolean(DataType t);
//   Returns true only for Boolean.

bool is_nullable_type(DataType t);
//   Returns true for all supported types, because every supported column type
//   may contain Arrow nulls.

std::string type_name(DataType t);
//   Returns a stable human-readable string such as "int32", "float64", etc.
//   This should be used in error messages so that tests and debugging output
//   are consistent.

DataType promote_numeric(DataType left, DataType right);
//   Implements arithmetic promotion rules for numeric operations only.
//   Expected behavior:
//   - int32 + int64 -> int64
//   - int32 + float32 -> float32
//   - int32 + float64 -> float64
//   - int64 + float32 -> float32 or float64 depending on your chosen rule,
//     but it must be consistent everywhere in the library.
//   - float32 + float64 -> float64
//   The rule must always preserve correctness and never silently narrow values.

DataType promote_for_comparison(DataType left, DataType right);
//   Validates whether two operands may be compared.
//   Numeric comparisons may require promotion.
//   String/string and boolean/boolean comparisons are allowed.
//   Mixed incompatible comparisons must fail immediately.

bool can_add(DataType left, DataType right);
bool can_subtract(DataType left, DataType right);
bool can_multiply(DataType left, DataType right);
bool can_divide(DataType left, DataType right);
bool can_modulo(DataType left, DataType right);
//   These helpers define whether a binary arithmetic operator is legal.

bool can_compare(DataType left, DataType right);
//   Returns true only if the comparison is semantically valid.
//   Incompatible types must be rejected before any execution begins.

bool can_apply_boolean_op(DataType left, DataType right);
//   Returns true only when both sides are boolean.

bool can_apply_string_op(DataType t);
//   Returns true only when the operand is a string.

DataType result_type_for_unary_abs(DataType input);
//   Returns the result type of abs(x). For integers and floats, the result
//   should preserve the input numeric family, unless your chosen design
//   intentionally promotes small integers to a wider integer type.

DataType result_type_for_length(DataType input);
//   Returns Int32 for string length operations.
