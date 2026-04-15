#include "../include/DataFrameLib/Errors.h"
#include "../include/DataFrameLib/Types.h"
#include "../include/DataFrameLib/ArrowUtils.h"

#include <cmath>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "arrow/builder.h"
#include "arrow/compute/api.h"
#include "arrow/scalar.h"

namespace {

std::string unsupported_arrow_type_message(const std::shared_ptr<arrow::DataType>& t) {
    std::ostringstream oss;
    oss << "unsupported Arrow type: ";
    if (!t) {
        oss << "<null>";
    } else {
        oss << t->ToString();
    }
    return oss.str();
}

template <typename BuilderT, typename ValueT>
arrow::Status append_value(BuilderT* builder, const ValueT& value) {
    return builder->Append(value);
}

arrow::Status append_literal_to_builder(
    arrow::ArrayBuilder* builder,
    const LiteralValue& value,
    DataType target_type) {

    if (std::holds_alternative<NullType>(value)) {
        return builder->AppendNull();
    }

    switch (target_type) {
        case DataType::Int32: {
            auto* b = dynamic_cast<arrow::Int32Builder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for int32");

            if (std::holds_alternative<int32_t>(value)) {
                return b->Append(std::get<int32_t>(value));
            }
            if (std::holds_alternative<int64_t>(value)) {
                auto v = std::get<int64_t>(value);
                if (v < std::numeric_limits<int32_t>::min() || v > std::numeric_limits<int32_t>::max()) {
                    return arrow::Status::Invalid("cannot safely convert int64 literal to int32");
                }
                return b->Append(static_cast<int32_t>(v));
            }
            return arrow::Status::Invalid("literal is not compatible with int32");
        }

        case DataType::Int64: {
            auto* b = dynamic_cast<arrow::Int64Builder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for int64");

            if (std::holds_alternative<int32_t>(value)) {
                return b->Append(static_cast<int64_t>(std::get<int32_t>(value)));
            }
            if (std::holds_alternative<int64_t>(value)) {
                return b->Append(std::get<int64_t>(value));
            }
            return arrow::Status::Invalid("literal is not compatible with int64");
        }

        case DataType::Float32: {
            auto* b = dynamic_cast<arrow::FloatBuilder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for float32");

            if (std::holds_alternative<int32_t>(value)) {
                return b->Append(static_cast<float>(std::get<int32_t>(value)));
            }
            if (std::holds_alternative<float>(value)) {
                return b->Append(std::get<float>(value));
            }
            return arrow::Status::Invalid("literal is not compatible with float32");
        }

        case DataType::Float64: {
            auto* b = dynamic_cast<arrow::DoubleBuilder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for float64");

            if (std::holds_alternative<int32_t>(value)) {
                return b->Append(static_cast<double>(std::get<int32_t>(value)));
            }
            if (std::holds_alternative<int64_t>(value)) {
                return b->Append(static_cast<double>(std::get<int64_t>(value)));
            }
            if (std::holds_alternative<float>(value)) {
                return b->Append(static_cast<double>(std::get<float>(value)));
            }
            if (std::holds_alternative<double>(value)) {
                return b->Append(std::get<double>(value));
            }
            return arrow::Status::Invalid("literal is not compatible with float64");
        }

        case DataType::String: {
            auto* b = dynamic_cast<arrow::StringBuilder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for string");

            if (std::holds_alternative<std::string>(value)) {
                return b->Append(std::get<std::string>(value));
            }
            return arrow::Status::Invalid("literal is not compatible with string");
        }

        case DataType::Boolean: {
            auto* b = dynamic_cast<arrow::BooleanBuilder*>(builder);
            if (!b) return arrow::Status::Invalid("internal builder mismatch for boolean");

            if (std::holds_alternative<bool>(value)) {
                return b->Append(std::get<bool>(value));
            }
            return arrow::Status::Invalid("literal is not compatible with boolean");
        }
    }

    return arrow::Status::Invalid("unknown target type");
}

} // namespace

arrow::Result<std::shared_ptr<arrow::DataType>> to_arrow_type(DataType t) {
    switch (t) {
        case DataType::Int32:
            return arrow::int32();
        case DataType::Int64:
            return arrow::int64();
        case DataType::Float32:
            return arrow::float32();
        case DataType::Float64:
            return arrow::float64();
        case DataType::String:
            return arrow::utf8();
        case DataType::Boolean:
            return arrow::boolean();
    }

    return arrow::Status::Invalid("unknown logical type");
}

DataType from_arrow_type(const std::shared_ptr<arrow::DataType>& t) {
    if (!t) {
        throw SchemaError("null Arrow type pointer");
    }

    switch (t->id()) {
        case arrow::Type::INT32:
            return DataType::Int32;
        case arrow::Type::INT64:
            return DataType::Int64;
        case arrow::Type::FLOAT:
            return DataType::Float32;
        case arrow::Type::DOUBLE:
            return DataType::Float64;
        case arrow::Type::STRING:
            return DataType::String;
        case arrow::Type::BOOL:
            return DataType::Boolean;
        default:
            throw SchemaError(unsupported_arrow_type_message(t));
    }
}

arrow::Result<std::shared_ptr<arrow::Scalar>> make_scalar(const LiteralValue& v) {
    if (std::holds_alternative<NullType>(v)) {
        return arrow::Status::Invalid(
            "cannot infer Arrow scalar type from NullType without context");
    }

    if (std::holds_alternative<int32_t>(v)) {
        return std::make_shared<arrow::Int32Scalar>(std::get<int32_t>(v));
    }
    if (std::holds_alternative<int64_t>(v)) {
        return std::make_shared<arrow::Int64Scalar>(std::get<int64_t>(v));
    }
    if (std::holds_alternative<float>(v)) {
        return std::make_shared<arrow::FloatScalar>(std::get<float>(v));
    }
    if (std::holds_alternative<double>(v)) {
        return std::make_shared<arrow::DoubleScalar>(std::get<double>(v));
    }
    if (std::holds_alternative<std::string>(v)) {
        return std::make_shared<arrow::StringScalar>(std::get<std::string>(v));
    }
    if (std::holds_alternative<bool>(v)) {
        return std::make_shared<arrow::BooleanScalar>(std::get<bool>(v));
    }

    return arrow::Status::Invalid("unsupported literal value");
}

arrow::Result<std::shared_ptr<arrow::Array>> make_array_from_literals(
    const std::vector<LiteralValue>& values,
    DataType target_type) {

    ARROW_ASSIGN_OR_RAISE(auto arrow_type, to_arrow_type(target_type));

    std::unique_ptr<arrow::ArrayBuilder> builder;
    ARROW_RETURN_NOT_OK(arrow::MakeBuilder(arrow::default_memory_pool(), arrow_type, &builder));

    for (const auto& v : values) {
        ARROW_RETURN_NOT_OK(append_literal_to_builder(builder.get(), v, target_type));
    }

    std::shared_ptr<arrow::Array> out;
    ARROW_RETURN_NOT_OK(builder->Finish(&out));
    return out;
}

bool scalar_is_null(const std::shared_ptr<arrow::Scalar>& s) {
    return !s || !s->is_valid;
}

// One more thing: if your Arrow version does not expose arrow::compute::Cast exactly this way, 
// the cast_array function may need a tiny API adjustment, but the structure below is the right one.

std::shared_ptr<arrow::Array> cast_array(
    const std::shared_ptr<arrow::Array>& input,
    DataType target_type) {

    if (!input) {
        throw SchemaError("cannot cast a null array");
    }

    auto arrow_type_result = to_arrow_type(target_type);
    if (!arrow_type_result.ok()) {
        throw SchemaError(arrow_type_result.status().ToString());
    }

    arrow::compute::CastOptions options = arrow::compute::CastOptions::Safe();
    auto cast_result = arrow::compute::Cast(arrow::Datum(input), arrow_type_result.ValueOrDie(), options);

    if (!cast_result.ok()) {
        throw SchemaError(cast_result.status().ToString());
    }

    arrow::Datum datum = std::move(cast_result.ValueOrDie());
    if (!datum.is_array()) {
        throw SchemaError("cast did not return an Arrow array");
    }

    return datum.make_array();
}