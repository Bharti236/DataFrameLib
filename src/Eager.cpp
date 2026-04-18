// ------------------------------------------------------------
// src/Eager.cpp
// ------------------------------------------------------------
//
// This file should contain the execution logic for all eager operations.
//
// It should implement:
//
// - DataFrame validation on construction
// - select by column names
// - select by expressions
// - filter
// - with_column
// - group_by helper construction
// - grouped aggregate execution
// - join
// - sort
// - head
// - writer forwarding helpers
//
// Core implementation rule:
// Every method must return a fresh EagerDataFrame.
// No method should modify the current object's table in place.
//
// Null semantics to preserve here:
// - Expression evaluation must not use NaN as a missing-value marker.
// - Any operation involving a null operand should generate a null result.
// - Aggregations should ignore nulls where mathematically appropriate,
//   but preserve nulls when a group has no valid input.
//
// Performance note:
// - Do not copy whole tables more than necessary.
// - Prefer Arrow slices, views, and builders where possible.
// - Use move semantics for newly built tables when safe.

#include "../include/DataFrameLib/IO.h"
#include "../include/DataFrameLib/Eager.h"
#include "../include/DataFrameLib/Errors.h"

#include <arrow/api.h>
#include <arrow/array/util.h>
#include <arrow/compute/api.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

[[noreturn]] void eager_fail(const std::string& msg) {
    throw DataFrameError("EagerDataFrame: " + msg);
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

JoinType parse_join_type_or_throw(const std::string& how) {
    const std::string normalized = lowercase_ascii(how);
    if (normalized == "inner") {
        return JoinType::Inner;
    }
    if (normalized == "left") {
        return JoinType::Left;
    }
    if (normalized == "right") {
        return JoinType::Right;
    }
    if (normalized == "full") {
        return JoinType::Full;
    }

    throw DataFrameError("invalid join type: " + how);
}

std::shared_ptr<arrow::Array> combine_single_column(const std::shared_ptr<arrow::ChunkedArray>& chunked) {
    if (!chunked) {
        eager_fail("encountered null chunked array");
    }

    if (chunked->num_chunks() == 0) {
        auto empty_res = arrow::MakeEmptyArray(chunked->type(), arrow::default_memory_pool());
        if (!empty_res.ok()) {
            eager_fail("failed to materialize empty Arrow array: " + empty_res.status().ToString());
        }
        return empty_res.ValueOrDie();
    }

    if (chunked->num_chunks() == 1) {
        return chunked->chunk(0);
    }

    auto table = arrow::Table::Make(
        arrow::schema({arrow::field("__combined__", chunked->type())}),
        {chunked}
    );
    auto combined_res = table->CombineChunks(arrow::default_memory_pool());
    if (!combined_res.ok()) {
        eager_fail("failed to combine Arrow chunks: " + combined_res.status().ToString());
    }

    auto combined_column = combined_res.ValueOrDie()->column(0);
    if (!combined_column || combined_column->num_chunks() != 1) {
        eager_fail("combined Arrow column did not materialize into a single chunk");
    }

    return combined_column->chunk(0);
}

std::shared_ptr<arrow::Table> make_table_from_columns(
    const std::vector<std::shared_ptr<arrow::Field>>& fields,
    const std::vector<std::shared_ptr<arrow::ChunkedArray>>& columns) {

    auto schema = arrow::schema(fields);
    return arrow::Table::Make(std::move(schema), columns);
}

} // namespace

DataType EagerDataFrame::arrow_type_to_datatype(const std::shared_ptr<arrow::DataType>& type) {
    if (!type) {
        throw TypeError("null Arrow type");
    }

    switch (type->id()) {
        case arrow::Type::INT32:        return DataType::Int32;
        case arrow::Type::INT64:        return DataType::Int64;
        case arrow::Type::FLOAT:        return DataType::Float32;
        case arrow::Type::DOUBLE:       return DataType::Float64;
        case arrow::Type::STRING:
        case arrow::Type::LARGE_STRING: return DataType::String;
        case arrow::Type::BOOL:         return DataType::Boolean;
        default:
            throw TypeError("unsupported Arrow type: " + type->ToString());
    }
}

void EagerDataFrame::validate_table(const std::shared_ptr<arrow::Table>& table) {
    if (!table) {
        throw SchemaError("Arrow table is null");
    }

    const auto schema_ptr = table->schema();
    if (!schema_ptr) {
        throw SchemaError("Arrow table schema is null");
    }

    std::unordered_set<std::string> seen;
    for (const auto& field : schema_ptr->fields()) {
        if (!field) {
            throw SchemaError("schema contains a null field");
        }
        if (!seen.insert(field->name()).second) {
            throw SchemaError("duplicate column name: " + field->name());
        }
        (void)arrow_type_to_datatype(field->type());
    }

    const std::int64_t rows = table->num_rows();
    for (int i = 0; i < table->num_columns(); ++i) {
        const auto& col = table->column(i);
        if (!col) {
            throw SchemaError("schema contains a null column");
        }
        if (col->length() != rows) {
            throw DimensionError("column length mismatch in Arrow table");
        }
    }
}

std::size_t EagerDataFrame::column_index(const std::string& name) const {
    if (!schema_) {
        throw SchemaError("schema is null");
    }

    const auto idx = schema_->GetFieldIndex(name);
    if (idx < 0) {
        throw ColumnNotFoundError("missing column: " + name);
    }
    return static_cast<std::size_t>(idx);
}

EagerDataFrame::EagerDataFrame(std::shared_ptr<arrow::Table> table)
    : table_(std::move(table)),
      schema_(table_ ? table_->schema() : nullptr) {
    validate_table(table_);
}

EagerDataFrame::EagerDataFrame(const std::map<std::string, Column>& columns) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::ChunkedArray>> arrays;
    fields.reserve(columns.size());
    arrays.reserve(columns.size());

    std::int64_t expected_rows = -1;
    for (const auto& [name, column] : columns) {
        if (!column.data()) {
            throw SchemaError("column '" + name + "' has null Arrow data");
        }

        const auto rows = static_cast<std::int64_t>(column.size());
        if (expected_rows < 0) {
            expected_rows = rows;
        } else if (expected_rows != rows) {
            throw DimensionError("column length mismatch while constructing eager dataframe");
        }

        fields.push_back(arrow::field(name, column.data()->type()));
        arrays.push_back(std::make_shared<arrow::ChunkedArray>(column.data()));
    }

    table_ = make_table_from_columns(fields, arrays);
    schema_ = table_->schema();
    validate_table(table_);
}

std::size_t EagerDataFrame::row_count() const {
    return table_ ? static_cast<std::size_t>(table_->num_rows()) : 0;
}

std::size_t EagerDataFrame::column_count() const {
    return table_ ? static_cast<std::size_t>(table_->num_columns()) : 0;
}

const arrow::Schema& EagerDataFrame::schema() const {
    if (!schema_) {
        throw SchemaError("schema is null");
    }
    return *schema_;
}

std::shared_ptr<arrow::Table> EagerDataFrame::to_arrow_table() const {
    return table_;
}

bool EagerDataFrame::has_column(const std::string& name) const {
    return schema_ && schema_->GetFieldIndex(name) >= 0;
}

Column EagerDataFrame::column(const std::string& name) const {
    const auto idx = column_index(name);
    const auto field = schema_->field(static_cast<int>(idx));
    if (!field) {
        throw SchemaError("null field for column: " + name);
    }

    auto arr = combine_single_column(table_->column(static_cast<int>(idx)));
    return Column(field->name(), arrow_type_to_datatype(field->type()), std::move(arr));
}

EagerDataFrame EagerDataFrame::select(const std::vector<std::string>& columns) const {
    if (!table_) {
        eager_fail("select on null table");
    }

    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::ChunkedArray>> arrays;
    fields.reserve(columns.size());
    arrays.reserve(columns.size());

    for (const auto& name : columns) {
        const auto idx = column_index(name);
        fields.push_back(schema_->field(static_cast<int>(idx)));
        arrays.push_back(table_->column(static_cast<int>(idx)));
    }

    return EagerDataFrame(make_table_from_columns(fields, arrays));
}

EagerDataFrame EagerDataFrame::select(const std::vector<Expression>& expressions) const {
    (void)expressions;
    throw DataFrameError(
        "select(vector<Expression>) requires an expression execution engine; "
        "the current Expression API exposes infer_type() but not evaluation"
    );
}

EagerDataFrame EagerDataFrame::filter(const Expression& predicate) const {
    (void)predicate;
    throw DataFrameError(
        "filter(Expression) requires expression evaluation support; not available yet"
    );
}

EagerDataFrame EagerDataFrame::with_column(const std::string& name,
                                          const Expression& expr) const {
    (void)name;
    (void)expr;
    throw DataFrameError(
        "with_column(name, expr) requires expression evaluation support; not available yet"
    );
}

EagerGroupBy EagerDataFrame::group_by(const std::vector<std::string>& keys) const {
    for (const auto& key : keys) {
        (void)column_index(key);
    }
    return EagerGroupBy(*this, keys);
}

EagerDataFrame EagerDataFrame::join(const EagerDataFrame& other,
                                    const std::vector<std::string>& on,
                                    JoinType how) const {
    (void)other;
    (void)on;
    (void)how;
    throw DataFrameError("join(...) is not implemented yet in this eager Arrow-only layer");
}

EagerDataFrame EagerDataFrame::join(const EagerDataFrame& other,
                                    const std::vector<std::string>& on,
                                    const std::string& how) const {
    return join(other, on, parse_join_type_or_throw(how));
}

EagerDataFrame EagerDataFrame::sort(const std::vector<std::string>& columns,
                                    bool ascending) const {
    (void)columns;
    (void)ascending;
    throw DataFrameError("sort(...) is not implemented yet in this eager Arrow-only layer");
}

EagerDataFrame EagerDataFrame::head(std::size_t n) const {
    if (!table_) {
        eager_fail("head on null table");
    }
    const std::size_t take = std::min(n, row_count());
    return EagerDataFrame(table_->Slice(0, static_cast<int64_t>(take)));
}

void EagerDataFrame::write_csv(const std::string& path) const {
    ::write_csv(*this, path);
}

void EagerDataFrame::write_parquet(const std::string& path) const {
    ::write_parquet(*this, path);
}

std::string EagerDataFrame::to_string() const {
    std::ostringstream out;
    out << "EagerDataFrame(";
    out << "rows=" << row_count() << ", cols=" << column_count() << ")\n";

    if (!schema_) {
        out << "<null schema>\n";
        return out.str();
    }

    out << "schema: [";
    for (int i = 0; i < schema_->num_fields(); ++i) {
        if (i) out << ", ";
        const auto field = schema_->field(i);
        out << field->name() << ":" << field->type()->ToString();
    }
    out << "]\n";
    return out.str();
}

void EagerDataFrame::validate() const {
    validate_table(table_);
}

EagerGroupBy::EagerGroupBy(const EagerDataFrame& parent, std::vector<std::string> keys)
    : parent_(&parent), keys_(std::move(keys)) {
    for (const auto& key : keys_) {
        if (!parent_->has_column(key)) {
            throw ColumnNotFoundError("missing group-by key: " + key);
        }
    }
}

EagerDataFrame EagerGroupBy::aggregate(const std::map<std::string, Expression>& aggs) const {
    (void)aggs;
    throw DataFrameError(
        "group_by(...).aggregate(...) requires expression aggregation evaluation; not available yet"
    );
}

EagerDataFrame EagerDataFrame::read_csv(const std::string& path) {
    return ::read_csv(path);
}

EagerDataFrame EagerDataFrame::read_parquet(const std::string& path) {
    return ::read_parquet(path);
}

EagerDataFrame from_columns(const std::map<std::string, Column>& columns) {
    return EagerDataFrame(columns);
}
