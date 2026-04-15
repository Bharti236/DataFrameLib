#include <string>
#include <stdexcept>
#include "Types.h"

// ===============================
// include/DataFrameLib/Errors.h
// ===============================
//
// Purpose:
// Centralized exception hierarchy for all type-safety and column-level failures.
//
// Why this file matters:
// The assignment says incompatible operations must throw immediate errors.
// These exceptions must be precise, readable, and reused everywhere.
//
// Recommended structure:

class DataFrameError : public std::runtime_error {
public:
    explicit DataFrameError(const std::string& message);
};

class TypeError : public DataFrameError {
public:
    explicit TypeError(const std::string& message);
};

class NullError : public DataFrameError {
public:
    explicit NullError(const std::string& message);
};

class SchemaError : public DataFrameError {
public:
    explicit SchemaError(const std::string& message);
};

class ColumnNotFoundError : public DataFrameError {
public:
    explicit ColumnNotFoundError(const std::string& message);
};

class DimensionError : public DataFrameError {
public:
    explicit DimensionError(const std::string& message);
};

// Functions that should exist:

[[noreturn]] void throw_type_error(const std::string& op,
                                   DataType left,
                                   DataType right);
//
//   Builds a uniform error message for invalid binary operations.
//   Example: "invalid operation '+' between int32 and string".
//
[[noreturn]] void throw_unary_type_error(const std::string& op,
                                        DataType input);
//
//   Used for invalid unary expressions like abs(string).
//
[[noreturn]] void throw_null_policy_error(const std::string& context);
//
//   Used only if the user violates a strict semantic rule that cannot be
//   expressed as a type mismatch.
