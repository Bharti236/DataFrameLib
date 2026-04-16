#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Types.h"
#include "arrow/api.h"

// ===============================
// include/DataFrameLib/Column.h
// ===============================

// Purpose:
// This is the core Step 1 abstraction.
// A Column is an immutable, Arrow-backed, typed, null-aware sequence of values.

// Important requirements:
// - The type of a column never changes after creation.
// - The data storage must be Arrow-backed.
// - All transformations must return new Column objects.
// - Null semantics must be strict: any operation involving a null operand
//   must produce a null result.
// - No NaN-based missing-value shortcuts are allowed.

// Suggested class design:

class Column {
public:
    // Construct a fully typed immutable column.
    Column(std::string name,
           DataType type,
           std::shared_ptr<arrow::Array> data);

    // Basic metadata access.
    const std::string& name() const;
    DataType type() const;
    const std::shared_ptr<arrow::Array>& data() const;
    std::size_t size() const;
    std::size_t null_count() const;
    bool empty() const;

    // Random access / inspection.
    bool is_null(std::size_t row) const;
    LiteralValue value_at(std::size_t row) const;
    // value_at may return NullType for null positions.
    // It should never expose internal Arrow mutation.

    // Immutability-preserving transforms.
    Column renamed(const std::string& new_name) const;
    Column sliced(std::size_t offset, std::size_t length) const;
    Column taken(const std::vector<int64_t>& indices) const;

    // Type manipulation.
    Column casted(DataType target_type) const;
    // This should apply Arrow casting only if the promotion/conversion is valid.
    // Example: int32 -> float64 may be allowed.
    // Example: string -> int32 should be rejected.

    // Unary operations.
    Column apply_abs() const;
    Column apply_is_null() const;
    Column apply_is_not_null() const;
    Column apply_length() const;
    Column apply_to_lower() const;
    Column apply_to_upper() const;

    // Binary arithmetic.
    Column add(const Column& rhs) const;
    Column subtract(const Column& rhs) const;
    Column multiply(const Column& rhs) const;
    Column divide(const Column& rhs) const;
    Column modulo(const Column& rhs) const;

    // Comparisons.
    Column equal(const Column& rhs) const;
    Column not_equal(const Column& rhs) const;
    Column less_than(const Column& rhs) const;
    Column less_equal(const Column& rhs) const;
    Column greater_than(const Column& rhs) const;
    Column greater_equal(const Column& rhs) const;

    // Boolean logic.
    Column logical_and(const Column& rhs) const;
    Column logical_or(const Column& rhs) const;
    Column logical_not() const;

    // String predicates.
    Column contains(const std::string& needle) const;
    Column starts_with(const std::string& prefix) const;
    Column ends_with(const std::string& suffix) const;

private:
    std::string name_;
    DataType type_;
    std::shared_ptr<arrow::Array> data_;
};

// Detailed behavior expected from each method:

// Column::Column(...)
//   - Validates that the Arrow array matches the declared logical type.
//   - Throws SchemaError or TypeError if the input array type is unsupported.
//   - Ensures the column is immediately usable and immutable.

// renamed(new_name)
//   - Returns a new Column with the same data and type, different name.
//   - Must not modify the original object.

// sliced(offset, length)
//   - Returns a view or materialized slice of the same typed data.
//   - Used later for head, filtering pipelines, and plan execution.

// taken(indices)
//   - Produces a new column containing the selected row positions.
//   - Index order must be preserved.
//   - Out-of-range indices should throw DimensionError.

// casted(target_type)
//   - Applies only safe and explicitly allowed conversions.
//   - Must respect promotion rules.
//   - Must reject unsupported conversions immediately.

// apply_abs()
//   - Only valid for numeric types.
//   - Output type should follow the chosen numeric rule.
//   - Null input values remain null.

// apply_is_null()
//   - Returns a Boolean column where true means the source value is null.

// apply_is_not_null()
//   - Returns the boolean inverse of apply_is_null().

// apply_length()
//   - Valid only for string columns.
//   - Result must be Int32 unless your design explicitly chooses Int64.
//   - Null input values remain null.
//
// apply_to_lower() / apply_to_upper()
//   - Valid only for string columns.
//   - Preserve nulls.
//   - Return string columns.
//
// add/subtract/multiply/divide/modulo()
//   - Validate operand types before execution.
//   - Apply promotion rules first.
//   - Result type is determined by promotion.
//   - Any null operand at a row produces a null result at that row.
//   - Division by zero behavior should be defined carefully later, but in this
//     step the focus is only on type correctness and null propagation.
//
// equal/not_equal/less_than/... comparisons
//   - Must verify that the two operand types are comparable.
//   - Result type must be Boolean.
//   - Null propagation rules must be enforced row-wise.
//
// logical_and/logical_or/logical_not()
//   - Only valid for boolean columns.
//   - Must preserve nulls.
//
// contains/starts_with/ends_with()
//   - Only valid for string columns.
//   - Return Boolean columns.
//   - Preserve nulls.
//
//
//
