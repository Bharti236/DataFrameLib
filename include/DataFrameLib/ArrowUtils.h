#pragma once

#include "Types.h"
#include <memory>
#include <vector>
#include "arrow/api.h"

// ===============================
// include/DataFrameLib/ArrowUtils.h
// ===============================
//
// Purpose:
// Thin Arrow-facing utility layer.
// This file isolates Arrow builder, array casting, scalar conversion,
// and null-handling details from the rest of the codebase.
//
// Design goal:
// Every other component should talk in terms of DataType / LiteralValue /
// column wrappers, while this file handles the actual Arrow plumbing.
//
// Functions that should exist:

arrow::Result<std::shared_ptr<arrow::DataType>> to_arrow_type(DataType t);
//   Converts the library's logical type into the corresponding Arrow type.

DataType from_arrow_type(const std::shared_ptr<arrow::DataType>& t);
//   Converts an Arrow type into the library's logical type.
//   If an unsupported Arrow type is seen, throw SchemaError.

arrow::Result<std::shared_ptr<arrow::Scalar>> make_scalar(const LiteralValue& v);
//   Creates an Arrow scalar from a typed literal.
//   If the literal is NullType, the returned scalar must be a typed null scalar
//   only when the target type is known from context.

arrow::Result<std::shared_ptr<arrow::Array>> make_array_from_literals(
    const std::vector<LiteralValue>& values,
    DataType target_type);
//   Builds a typed Arrow array from a literal vector.
//   All values must be validated against the requested target_type.
//   Nulls must become Arrow nulls.

bool scalar_is_null(const std::shared_ptr<arrow::Scalar>& s);
//   Convenience helper used when evaluating expressions.

std::shared_ptr<arrow::Array> cast_array(
    const std::shared_ptr<arrow::Array>& input,
    DataType target_type);
//   Performs safe casting with Arrow.
//   This should be used only when a promotion rule says the cast is valid.
//   Unsafe or narrowing casts should not be allowed silently.

std::shared_ptr<arrow::Array> replace_nulls_with_sentinel(
    const std::shared_ptr<arrow::Array>& input,
    DataType target_type,
    const LiteralValue& sentinel);
//   Not for final semantics.
//   Prefer not to use sentinels in the library.
//   This helper should exist only if you need it internally for temporary logic.
//   Final missing-value behavior must still use Arrow nulls.
