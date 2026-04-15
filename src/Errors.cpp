#include "../include/DataFrameLib/Errors.h"
#include "../include/DataFrameLib/Types.h"

#include <sstream>
#include <utility>

// -------------------------------
// Exception class definitions
// -------------------------------

DataFrameError::DataFrameError(const std::string& message)
    : std::runtime_error(message) {}

TypeError::TypeError(const std::string& message)
    : DataFrameError(message) {}

NullError::NullError(const std::string& message)
    : DataFrameError(message) {}

SchemaError::SchemaError(const std::string& message)
    : DataFrameError(message) {}

ColumnNotFoundError::ColumnNotFoundError(const std::string& message)
    : DataFrameError(message) {}

DimensionError::DimensionError(const std::string& message)
    : DataFrameError(message) {}

// -------------------------------
// Helper for building messages
// -------------------------------

namespace {
std::string binary_type_error_message(const std::string& op,
                                      DataType left,
                                      DataType right) {
    std::ostringstream oss;
    oss << "invalid operation '" << op << "' between "
        << type_name(left) << " and " << type_name(right);
    return oss.str();
}

std::string unary_type_error_message(const std::string& op,
                                     DataType input) {
    std::ostringstream oss;
    oss << "invalid operation '" << op << "' on " << type_name(input);
    return oss.str();
}
} // namespace

// -------------------------------
// Throw helpers
// -------------------------------

[[noreturn]] void throw_type_error(const std::string& op,
                                   DataType left,
                                   DataType right) {
    throw TypeError(binary_type_error_message(op, left, right));
}

[[noreturn]] void throw_unary_type_error(const std::string& op,
                                         DataType input) {
    throw TypeError(unary_type_error_message(op, input));
}

[[noreturn]] void throw_null_policy_error(const std::string& context) {
    throw NullError("null policy violation: " + context);
}