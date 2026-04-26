// ------------------------------------------------------------
//
// src/Lazy.cpp
// ------------------------------------------------------------
//
// Step 4 responsibilities:
// 1) Build a logical DAG instead of executing operations immediately.
// 2) Keep source nodes (scan_csv / scan_parquet) lazy.
// 3) Materialize the DAG only when collect() / sink_*() / explain() is called.
//
// This file therefore has two halves:
// - plan construction helpers,
// - a small logical-plan executor for the current project state.

#include "../include/DataFrameLib/Lazy.h"

#include "../include/DataFrameLib/ArrowUtils.h"
#include "../include/DataFrameLib/Errors.h"
#include "../include/DataFrameLib/IO.h"
#include "../include/DataFrameLib/Optimizer.h"

#include <graphviz/cgraph.h>
#include <graphviz/gvc.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void lazy_fail(const std::string& message) {
    throw DataFrameError("LazyDataFrame: " + message);
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
    if (normalized == "outer") {
        return JoinType::Full;
    }

    throw DataFrameError("invalid join type: " + how);
}

std::size_t next_plan_node_id() {
    static std::size_t next_id = 1;
    return next_id++;
}

LazyPlanNodePtr make_node(LazyNodeKind kind) {
    auto node = std::make_shared<LazyPlanNode>();
    node->id = next_plan_node_id();
    node->kind = kind;
    return node;
}

LazyPlanNodePtr make_unary_node(LazyNodeKind kind, const LazyPlanNodePtr& input) {
    if (!input) {
        lazy_fail("cannot build a lazy node from a null input plan");
    }

    auto node = make_node(kind);
    node->inputs.push_back(input);
    return node;
}

std::string join_vector(const std::vector<std::string>& values, const std::string& sep) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << sep;
        out << values[i];
    }
    return out.str();
}

std::string lazy_node_kind_name(LazyNodeKind kind) {
    switch (kind) {
        case LazyNodeKind::ScanCsv:           return "ScanCsv";
        case LazyNodeKind::ScanParquet:       return "ScanParquet";
        case LazyNodeKind::InMemorySource:    return "InMemorySource";
        case LazyNodeKind::SelectColumns:     return "SelectColumns";
        case LazyNodeKind::SelectExpressions: return "SelectExpressions";
        case LazyNodeKind::Filter:            return "Filter";
        case LazyNodeKind::WithColumn:        return "WithColumn";
        case LazyNodeKind::GroupBy:           return "GroupBy";
        case LazyNodeKind::Aggregate:         return "Aggregate";
        case LazyNodeKind::Join:              return "Join";
        case LazyNodeKind::Sort:              return "Sort";
        case LazyNodeKind::Head:              return "Head";
        case LazyNodeKind::SinkCsv:           return "SinkCsv";
        case LazyNodeKind::SinkParquet:       return "SinkParquet";
    }

    return "Unknown";
}

std::string join_type_name(JoinType kind) {
    switch (kind) {
        case JoinType::Inner: return "inner";
        case JoinType::Left:  return "left";
        case JoinType::Right: return "right";
        case JoinType::Full:  return "full";
    }

    return "unknown";
}

bool is_null_literal(const Expression& expr) {
    return expr.kind() == ExprKind::Literal &&
           expr.literal().has_value() &&
           std::holds_alternative<NullType>(*expr.literal());
}

std::string literal_to_string(const LiteralValue& value) {
    if (std::holds_alternative<NullType>(value)) {
        return "null";
    }
    if (std::holds_alternative<int32_t>(value)) {
        return std::to_string(std::get<int32_t>(value));
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::to_string(std::get<int64_t>(value));
    }
    if (std::holds_alternative<float>(value)) {
        std::ostringstream out;
        out << std::get<float>(value);
        return out.str();
    }
    if (std::holds_alternative<double>(value)) {
        std::ostringstream out;
        out << std::get<double>(value);
        return out.str();
    }
    if (std::holds_alternative<std::string>(value)) {
        return std::get<std::string>(value);
    }
    if (std::holds_alternative<bool>(value)) {
        return std::get<bool>(value) ? "true" : "false";
    }

    return "<unknown>";
}

std::string serialize_literal(const LiteralValue& value, DataType type) {
    std::ostringstream out;
    out << type_name(type) << ':';
    if (std::holds_alternative<NullType>(value)) {
        out << "null";
        return out.str();
    }

    if (type == DataType::String) {
        const auto& s = std::get<std::string>(value);
        out << s.size() << ':' << s;
        return out.str();
    }

    out << literal_to_string(value);
    return out.str();
}

long double to_long_double(const LiteralValue& value) {
    if (std::holds_alternative<int32_t>(value)) {
        return static_cast<long double>(std::get<int32_t>(value));
    }
    if (std::holds_alternative<int64_t>(value)) {
        return static_cast<long double>(std::get<int64_t>(value));
    }
    if (std::holds_alternative<float>(value)) {
        return static_cast<long double>(std::get<float>(value));
    }
    if (std::holds_alternative<double>(value)) {
        return static_cast<long double>(std::get<double>(value));
    }

    throw TypeError("expected numeric literal");
}

LiteralValue numeric_to_literal(long double value, DataType type) {
    switch (type) {
        case DataType::Int32:   return static_cast<int32_t>(value);
        case DataType::Int64:   return static_cast<int64_t>(value);
        case DataType::Float32: return static_cast<float>(value);
        case DataType::Float64: return static_cast<double>(value);
        default:
            throw TypeError("invalid numeric target type");
    }
}

int compare_literals(const LiteralValue& left, const LiteralValue& right, DataType type) {
    if (std::holds_alternative<NullType>(left) || std::holds_alternative<NullType>(right)) {
        throw NullError("cannot compare null literal values directly");
    }

    if (is_numeric(type)) {
        const long double l = to_long_double(left);
        const long double r = to_long_double(right);
        if (l < r) return -1;
        if (l > r) return 1;
        return 0;
    }

    if (type == DataType::String) {
        const auto& l = std::get<std::string>(left);
        const auto& r = std::get<std::string>(right);
        if (l < r) return -1;
        if (l > r) return 1;
        return 0;
    }

    if (type == DataType::Boolean) {
        const bool l = std::get<bool>(left);
        const bool r = std::get<bool>(right);
        if (l == r) return 0;
        return l ? 1 : -1;
    }

    throw TypeError("unsupported comparison type");
}

std::vector<Column> dataframe_columns(const EagerDataFrame& df) {
    std::vector<Column> columns;
    columns.reserve(df.column_count());

    const auto& schema = df.schema();
    for (int i = 0; i < schema.num_fields(); ++i) {
        columns.push_back(df.column(schema.field(i)->name()));
    }
    return columns;
}

EagerDataFrame dataframe_from_columns(const std::vector<Column>& columns, std::size_t row_count) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::shared_ptr<arrow::ChunkedArray>> arrays;
    fields.reserve(columns.size());
    arrays.reserve(columns.size());

    for (const auto& column : columns) {
        fields.push_back(arrow::field(column.name(), column.data()->type()));
        arrays.push_back(std::make_shared<arrow::ChunkedArray>(column.data()));
    }

    auto table = arrow::Table::Make(
        arrow::schema(fields),
        arrays,
        static_cast<int64_t>(row_count)
    );
    return from_arrow_table(std::move(table));
}

Column make_constant_column(const std::string& name,
                            DataType type,
                            const LiteralValue& value,
                            std::size_t row_count) {
    std::vector<LiteralValue> values(row_count, value);
    auto array_result = make_array_from_literals(values, type);
    if (!array_result.ok()) {
        lazy_fail("failed to build constant column '" + name + "': " +
                  array_result.status().ToString());
    }
    return Column(name, type, array_result.ValueOrDie());
}

Column make_all_null_column(const std::string& name, DataType type, std::size_t row_count) {
    return make_constant_column(name, type, LiteralValue{NullType{}}, row_count);
}

std::string default_expression_name(const Expression& expr, std::size_t ordinal) {
    if (!expr.alias_name().empty()) {
        return expr.alias_name();
    }

    if (expr.kind() == ExprKind::ColumnRef && expr.name().has_value()) {
        return *expr.name();
    }

    if (expr.kind() == ExprKind::Literal) {
        return "literal_" + std::to_string(ordinal);
    }

    if (expr.kind() == ExprKind::AggregateOp) {
        return "agg_" + std::to_string(ordinal);
    }

    return "expr_" + std::to_string(ordinal);
}

Column evaluate_expression(const Expression& expr,
                           const EagerDataFrame& df,
                           const std::string& output_name,
                           std::optional<DataType> type_hint = std::nullopt);

Column evaluate_alias_child(const Expression& expr,
                            const EagerDataFrame& df,
                            const std::string& output_name,
                            std::optional<DataType> type_hint) {
    if (expr.children().size() != 1 || !expr.children()[0]) {
        throw TypeError("alias expression must have exactly one child");
    }

    const std::string alias_name = expr.alias_name().empty() ? output_name : expr.alias_name();
    return evaluate_expression(*expr.children()[0], df, alias_name, type_hint).renamed(alias_name);
}

Column evaluate_expression(const Expression& expr,
                           const EagerDataFrame& df,
                           const std::string& output_name,
                           std::optional<DataType> type_hint) {
    const std::size_t row_count = df.row_count();

    switch (expr.kind()) {
        case ExprKind::Alias:
            return evaluate_alias_child(expr, df, output_name, type_hint);

        case ExprKind::ColumnRef:
            if (!expr.name().has_value()) {
                throw SchemaError("column reference has no name");
            }
            return df.column(*expr.name()).renamed(output_name);

        case ExprKind::Literal: {
            const DataType type = type_hint.has_value() ? *type_hint : expr.infer_type(df.schema());
            if (!expr.literal().has_value()) {
                throw TypeError("literal expression has no value");
            }
            return make_constant_column(output_name, type, *expr.literal(), row_count);
        }

        case ExprKind::IsNull: {
            const auto& child = expr.children();
            if (child.size() != 1 || !child[0]) {
                throw TypeError("is_null expression must have one child");
            }
            if (is_null_literal(*child[0])) {
                return make_constant_column(output_name, DataType::Boolean, LiteralValue{true}, row_count);
            }
            return evaluate_expression(*child[0], df, output_name).apply_is_null().renamed(output_name);
        }

        case ExprKind::IsNotNull: {
            const auto& child = expr.children();
            if (child.size() != 1 || !child[0]) {
                throw TypeError("is_not_null expression must have one child");
            }
            if (is_null_literal(*child[0])) {
                return make_constant_column(output_name, DataType::Boolean, LiteralValue{false}, row_count);
            }
            return evaluate_expression(*child[0], df, output_name).apply_is_not_null().renamed(output_name);
        }

        case ExprKind::UnaryOp: {
            const auto& child = expr.children();
            if (child.size() != 1 || !child[0]) {
                throw TypeError("unary expression must have one child");
            }

            if (expr.op() == OpKind::Not && is_null_literal(*child[0])) {
                return make_all_null_column(output_name, DataType::Boolean, row_count);
            }

            Column value = evaluate_expression(*child[0], df, output_name);
            switch (expr.op()) {
                case OpKind::Abs:
                    return value.apply_abs().renamed(output_name);
                case OpKind::Not:
                    return value.logical_not().renamed(output_name);
                default:
                    throw TypeError("unsupported unary operator in evaluation");
            }
        }

        case ExprKind::BinaryOp:
        case ExprKind::ComparisonOp:
        case ExprKind::BooleanOp:
        case ExprKind::StringOp: {
            const auto& child = expr.children();

            if ((expr.kind() == ExprKind::StringOp &&
                 (expr.op() == OpKind::Length || expr.op() == OpKind::ToLower || expr.op() == OpKind::ToUpper))) {
                if (child.size() != 1 || !child[0]) {
                    throw TypeError("unary string expression must have one child");
                }
                Column value = evaluate_expression(*child[0], df, output_name);
                switch (expr.op()) {
                    case OpKind::Length:
                        return value.apply_length().renamed(output_name);
                    case OpKind::ToLower:
                        return value.apply_to_lower().renamed(output_name);
                    case OpKind::ToUpper:
                        return value.apply_to_upper().renamed(output_name);
                    default:
                        break;
                }
            }

            if (child.size() != 2 || !child[0] || !child[1]) {
                throw TypeError("binary expression must have two children");
            }

            if (is_null_literal(*child[0]) || is_null_literal(*child[1])) {
                const DataType result_type = expr.infer_type(df.schema());
                return make_all_null_column(output_name, result_type, row_count);
            }

            Column lhs = evaluate_expression(*child[0], df, output_name + "_lhs");
            Column rhs = evaluate_expression(*child[1], df, output_name + "_rhs");

            switch (expr.kind()) {
                case ExprKind::BinaryOp:
                    switch (expr.op()) {
                        case OpKind::Add:
                            return lhs.add(rhs).renamed(output_name);
                        case OpKind::Subtract:
                            return lhs.subtract(rhs).renamed(output_name);
                        case OpKind::Multiply:
                            return lhs.multiply(rhs).renamed(output_name);
                        case OpKind::Divide:
                            return lhs.casted(DataType::Float64)
                                      .divide(rhs.casted(DataType::Float64))
                                      .renamed(output_name);
                        case OpKind::Modulo:
                            return lhs.modulo(rhs).renamed(output_name);
                        default:
                            throw TypeError("unsupported binary arithmetic operator");
                    }

                case ExprKind::ComparisonOp:
                    switch (expr.op()) {
                        case OpKind::Equal:
                            return lhs.equal(rhs).renamed(output_name);
                        case OpKind::NotEqual:
                            return lhs.not_equal(rhs).renamed(output_name);
                        case OpKind::LessThan:
                            return lhs.less_than(rhs).renamed(output_name);
                        case OpKind::LessEqual:
                            return lhs.less_equal(rhs).renamed(output_name);
                        case OpKind::GreaterThan:
                            return lhs.greater_than(rhs).renamed(output_name);
                        case OpKind::GreaterEqual:
                            return lhs.greater_equal(rhs).renamed(output_name);
                        default:
                            throw TypeError("unsupported comparison operator");
                    }

                case ExprKind::BooleanOp:
                    switch (expr.op()) {
                        case OpKind::And:
                            return lhs.logical_and(rhs).renamed(output_name);
                        case OpKind::Or:
                            return lhs.logical_or(rhs).renamed(output_name);
                        default:
                            throw TypeError("unsupported boolean operator");
                    }

                case ExprKind::StringOp:
                    switch (expr.op()) {
                        case OpKind::Contains:
                            if (!child[1]->literal().has_value() ||
                                !std::holds_alternative<std::string>(*child[1]->literal())) {
                                throw TypeError("contains() requires a string literal needle");
                            }
                            return lhs.contains(std::get<std::string>(*child[1]->literal())).renamed(output_name);
                        case OpKind::StartsWith:
                            if (!child[1]->literal().has_value() ||
                                !std::holds_alternative<std::string>(*child[1]->literal())) {
                                throw TypeError("starts_with() requires a string literal prefix");
                            }
                            return lhs.starts_with(std::get<std::string>(*child[1]->literal())).renamed(output_name);
                        case OpKind::EndsWith:
                            if (!child[1]->literal().has_value() ||
                                !std::holds_alternative<std::string>(*child[1]->literal())) {
                                throw TypeError("ends_with() requires a string literal suffix");
                            }
                            return lhs.ends_with(std::get<std::string>(*child[1]->literal())).renamed(output_name);
                        default:
                            throw TypeError("unsupported string operator");
                    }

                default:
                    break;
            }

            throw TypeError("unexpected binary expression kind");
        }

        case ExprKind::AggregateOp:
            throw TypeError("aggregate expressions cannot be evaluated row-wise");
    }

    throw TypeError("unsupported expression kind");
}

std::vector<int64_t> filter_indices(const Column& predicate) {
    if (predicate.type() != DataType::Boolean) {
        throw TypeError("filter predicate must evaluate to boolean");
    }

    std::vector<int64_t> indices;
    for (std::size_t i = 0; i < predicate.size(); ++i) {
        if (predicate.is_null(i)) {
            continue;
        }
        if (std::get<bool>(predicate.value_at(i))) {
            indices.push_back(static_cast<int64_t>(i));
        }
    }
    return indices;
}

EagerDataFrame take_rows(const EagerDataFrame& df, const std::vector<int64_t>& indices) {
    auto columns = dataframe_columns(df);
    for (auto& column : columns) {
        column = column.taken(indices);
    }
    return dataframe_from_columns(columns, indices.size());
}

LiteralValue aggregate_value(const Expression& expr, const EagerDataFrame& group_df) {
    const Expression* current = &expr;
    while (current->kind() == ExprKind::Alias) {
        if (current->children().size() != 1 || !current->children()[0]) {
            throw TypeError("alias aggregate expression must have one child");
        }
        current = current->children()[0].get();
    }

    if (current->kind() != ExprKind::AggregateOp || current->children().size() != 1 || !current->children()[0]) {
        throw TypeError("aggregate() expects aggregate expressions in the aggregation map");
    }

    const Expression& child_expr = *current->children()[0];
    if (current->op() == OpKind::Count && is_null_literal(child_expr)) {
        return LiteralValue{static_cast<int64_t>(0)};
    }

    Column values = evaluate_expression(child_expr, group_df, "agg_input");
    std::size_t valid_count = 0;
    LiteralValue best{};
    bool has_best = false;
    long double numeric_sum = 0.0L;

    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values.is_null(i)) {
            continue;
        }

        const LiteralValue value = values.value_at(i);
        ++valid_count;

        switch (current->op()) {
            case OpKind::Count:
                break;

            case OpKind::Sum:
            case OpKind::Mean:
                numeric_sum += to_long_double(value);
                break;

            case OpKind::Min:
                if (!has_best || compare_literals(value, best, values.type()) < 0) {
                    best = value;
                    has_best = true;
                }
                break;

            case OpKind::Max:
                if (!has_best || compare_literals(value, best, values.type()) > 0) {
                    best = value;
                    has_best = true;
                }
                break;

            default:
                throw TypeError("unsupported aggregate operator");
        }
    }

    switch (current->op()) {
        case OpKind::Count:
            return LiteralValue{static_cast<int64_t>(valid_count)};

        case OpKind::Sum: {
            if (valid_count == 0) {
                return LiteralValue{NullType{}};
            }
            const DataType out_type = current->infer_type(group_df.schema());
            return numeric_to_literal(numeric_sum, out_type);
        }

        case OpKind::Mean:
            if (valid_count == 0) {
                return LiteralValue{NullType{}};
            }
            return LiteralValue{static_cast<double>(numeric_sum / static_cast<long double>(valid_count))};

        case OpKind::Min:
        case OpKind::Max:
            if (!has_best) {
                return LiteralValue{NullType{}};
            }
            return best;

        default:
            break;
    }

    throw TypeError("unsupported aggregate operator");
}

std::string unique_name(std::unordered_set<std::string>& seen, const std::string& base) {
    std::string candidate = base;
    std::size_t suffix = 1;
    while (!seen.insert(candidate).second) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

std::map<std::string, Expression> normalize_named_aggregations(
    const std::vector<std::pair<std::string, std::string>>& aggs) {
    std::map<std::string, Expression> named;
    std::unordered_set<std::string> seen_names;

    for (const auto& [column_name, op_name] : aggs) {
        const std::string normalized_op = lowercase_ascii(op_name);

        Expression expr = [&]() -> Expression {
            if (normalized_op == "sum") {
                return col(column_name).sum();
            }
            if (normalized_op == "mean") {
                return col(column_name).mean();
            }
            if (normalized_op == "count") {
                return col(column_name).count();
            }
            if (normalized_op == "min") {
                return col(column_name).min();
            }
            if (normalized_op == "max") {
                return col(column_name).max();
            }

            throw DataFrameError("unsupported aggregation: " + op_name);
        }();

        const std::string output_name = unique_name(seen_names, column_name + "_" + normalized_op);
        named.emplace(output_name, std::move(expr));
    }

    return named;
}

std::string group_key_for_row(const std::vector<Column>& key_columns, std::size_t row) {
    std::ostringstream out;
    for (const auto& column : key_columns) {
        out << serialize_literal(column.value_at(row), column.type()) << '|';
    }
    return out.str();
}

EagerDataFrame execute_aggregate(const LazyPlanNodePtr& aggregate_node,
                                 const std::function<EagerDataFrame(const LazyPlanNodePtr&)>& execute_plan) {
    if (!aggregate_node || aggregate_node->inputs.size() != 1 || !aggregate_node->inputs[0]) {
        lazy_fail("aggregate node is malformed");
    }

    const auto& group_node = aggregate_node->inputs[0];
    if (group_node->kind != LazyNodeKind::GroupBy || group_node->inputs.size() != 1 || !group_node->inputs[0]) {
        lazy_fail("aggregate node must consume a GroupBy node");
    }

    EagerDataFrame input = execute_plan(group_node->inputs[0]);
    std::vector<Column> key_columns;
    key_columns.reserve(group_node->columns.size());
    for (const auto& key : group_node->columns) {
        key_columns.push_back(input.column(key));
    }

    std::unordered_map<std::string, std::size_t> group_lookup;
    std::vector<std::vector<int64_t>> groups;
    std::vector<std::vector<LiteralValue>> key_values;

    if (input.row_count() == 0 && group_node->columns.empty()) {
        groups.push_back({});
        key_values.push_back({});
    }

    for (std::size_t row = 0; row < input.row_count(); ++row) {
        const std::string key = group_key_for_row(key_columns, row);
        auto it = group_lookup.find(key);
        if (it == group_lookup.end()) {
            const std::size_t group_index = groups.size();
            group_lookup.emplace(key, group_index);
            groups.push_back({});

            std::vector<LiteralValue> values;
            values.reserve(key_columns.size());
            for (const auto& column : key_columns) {
                values.push_back(column.value_at(row));
            }
            key_values.push_back(std::move(values));
            it = group_lookup.find(key);
        }
        groups[it->second].push_back(static_cast<int64_t>(row));
    }

    std::vector<std::shared_ptr<arrow::Field>> fields;
    std::vector<std::vector<LiteralValue>> output_values;
    fields.reserve(group_node->columns.size() + aggregate_node->aggregations.size());
    output_values.reserve(fields.capacity());

    for (std::size_t i = 0; i < group_node->columns.size(); ++i) {
        fields.push_back(arrow::field(group_node->columns[i], key_columns[i].data()->type()));
        output_values.emplace_back();
        output_values.back().reserve(groups.size());
    }

    for (const auto& [name, expr] : aggregate_node->aggregations) {
        const DataType type = expr.infer_type(input.schema());
        auto type_result = to_arrow_type(type);
        if (!type_result.ok()) {
            lazy_fail("failed to derive Arrow type for aggregate column '" + name + "'");
        }
        fields.push_back(arrow::field(name, type_result.ValueOrDie()));
        output_values.emplace_back();
        output_values.back().reserve(groups.size());
    }

    for (std::size_t group_idx = 0; group_idx < groups.size(); ++group_idx) {
        const auto& rows = groups[group_idx];
        EagerDataFrame group_df = take_rows(input, rows);

        for (std::size_t key_idx = 0; key_idx < group_node->columns.size(); ++key_idx) {
            output_values[key_idx].push_back(key_values[group_idx][key_idx]);
        }

        std::size_t out_index = group_node->columns.size();
        for (const auto& [name, expr] : aggregate_node->aggregations) {
            (void)name;
            output_values[out_index++].push_back(aggregate_value(expr, group_df));
        }
    }

    std::vector<std::shared_ptr<arrow::ChunkedArray>> arrays;
    arrays.reserve(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const DataType type = fields[i]->type()->id() == arrow::Type::INT32 ? DataType::Int32 :
                              fields[i]->type()->id() == arrow::Type::INT64 ? DataType::Int64 :
                              fields[i]->type()->id() == arrow::Type::FLOAT ? DataType::Float32 :
                              fields[i]->type()->id() == arrow::Type::DOUBLE ? DataType::Float64 :
                              fields[i]->type()->id() == arrow::Type::BOOL ? DataType::Boolean :
                              DataType::String;

        auto array_result = make_array_from_literals(output_values[i], type);
        if (!array_result.ok()) {
            lazy_fail("failed to build aggregate output column '" + fields[i]->name() + "': " +
                      array_result.status().ToString());
        }
        arrays.push_back(std::make_shared<arrow::ChunkedArray>(array_result.ValueOrDie()));
    }

    auto table = arrow::Table::Make(
        arrow::schema(fields),
        arrays,
        static_cast<int64_t>(groups.size())
    );
    return from_arrow_table(std::move(table));
}

struct JoinOutputColumn {
    bool from_left = true;
    std::size_t index = 0;
    std::string name;
    DataType type = DataType::Int32;
};

bool row_keys_match(const std::vector<Column>& left_keys,
                    std::size_t left_row,
                    const std::vector<Column>& right_keys,
                    std::size_t right_row) {
    for (std::size_t i = 0; i < left_keys.size(); ++i) {
        const LiteralValue left = left_keys[i].value_at(left_row);
        const LiteralValue right = right_keys[i].value_at(right_row);

        if (std::holds_alternative<NullType>(left) || std::holds_alternative<NullType>(right)) {
            return false;
        }

        if (compare_literals(left, right, left_keys[i].type()) != 0) {
            return false;
        }
    }
    return true;
}

EagerDataFrame execute_join(const LazyPlanNodePtr& node,
                            const std::function<EagerDataFrame(const LazyPlanNodePtr&)>& execute_plan) {
    if (!node || node->inputs.size() != 2 || !node->inputs[0] || !node->inputs[1] || !node->join_type.has_value()) {
        lazy_fail("join node is malformed");
    }

    EagerDataFrame left_df = execute_plan(node->inputs[0]);
    EagerDataFrame right_df = execute_plan(node->inputs[1]);

    std::vector<Column> left_columns = dataframe_columns(left_df);
    std::vector<Column> right_columns = dataframe_columns(right_df);
    std::vector<Column> left_keys;
    std::vector<Column> right_keys;
    std::unordered_map<std::string, std::size_t> right_index_by_name;
    left_keys.reserve(node->columns.size());
    right_keys.reserve(node->columns.size());
    right_index_by_name.reserve(right_columns.size());
    for (std::size_t i = 0; i < right_columns.size(); ++i) {
        right_index_by_name.emplace(right_columns[i].name(), i);
    }
    for (const auto& key : node->columns) {
        left_keys.push_back(left_df.column(key));
        right_keys.push_back(right_df.column(key));
    }

    std::unordered_set<std::string> seen_names;
    std::unordered_set<std::string> join_keys(node->columns.begin(), node->columns.end());
    std::vector<JoinOutputColumn> output_columns;
    output_columns.reserve(left_columns.size() + right_columns.size());

    for (std::size_t i = 0; i < left_columns.size(); ++i) {
        seen_names.insert(left_columns[i].name());
        output_columns.push_back({true, i, left_columns[i].name(), left_columns[i].type()});
    }

    for (std::size_t i = 0; i < right_columns.size(); ++i) {
        if (join_keys.count(right_columns[i].name()) > 0) {
            continue;
        }

        std::string base = right_columns[i].name();
        if (seen_names.count(base) > 0) {
            base += "_right";
        }
        output_columns.push_back({false, i, unique_name(seen_names, base), right_columns[i].type()});
    }

    std::vector<std::vector<LiteralValue>> output_values(output_columns.size());
    std::vector<bool> right_matched(right_df.row_count(), false);

    auto append_join_row = [&](std::optional<std::size_t> left_row, std::optional<std::size_t> right_row) {
        for (std::size_t out_idx = 0; out_idx < output_columns.size(); ++out_idx) {
            const auto& spec = output_columns[out_idx];
            if (spec.from_left) {
                if (left_row.has_value()) {
                    output_values[out_idx].push_back(left_columns[spec.index].value_at(*left_row));
                } else if (right_row.has_value() &&
                           join_keys.count(left_columns[spec.index].name()) > 0 &&
                           right_index_by_name.count(left_columns[spec.index].name()) > 0) {
                    const std::size_t right_index = right_index_by_name.at(left_columns[spec.index].name());
                    output_values[out_idx].push_back(right_columns[right_index].value_at(*right_row));
                } else {
                    output_values[out_idx].push_back(NullType{});
                }
            } else {
                if (right_row.has_value()) {
                    output_values[out_idx].push_back(right_columns[spec.index].value_at(*right_row));
                } else {
                    output_values[out_idx].push_back(NullType{});
                }
            }
        }
    };

    for (std::size_t left_row = 0; left_row < left_df.row_count(); ++left_row) {
        bool matched = false;
        for (std::size_t right_row = 0; right_row < right_df.row_count(); ++right_row) {
            if (row_keys_match(left_keys, left_row, right_keys, right_row)) {
                matched = true;
                right_matched[right_row] = true;
                append_join_row(left_row, right_row);
            }
        }

        if (!matched && (node->join_type == JoinType::Left || node->join_type == JoinType::Full)) {
            append_join_row(left_row, std::nullopt);
        }
    }

    if (node->join_type == JoinType::Right || node->join_type == JoinType::Full) {
        for (std::size_t right_row = 0; right_row < right_df.row_count(); ++right_row) {
            if (!right_matched[right_row]) {
                append_join_row(std::nullopt, right_row);
            }
        }
    }

    if (node->join_type == JoinType::Inner && output_values.empty()) {
        return dataframe_from_columns({}, 0);
    }

    std::vector<Column> columns;
    columns.reserve(output_columns.size());
    for (std::size_t i = 0; i < output_columns.size(); ++i) {
        auto array_result = make_array_from_literals(output_values[i], output_columns[i].type);
        if (!array_result.ok()) {
            lazy_fail("failed to build join output column '" + output_columns[i].name + "'");
        }
        columns.emplace_back(output_columns[i].name, output_columns[i].type, array_result.ValueOrDie());
    }

    const std::size_t row_count = output_columns.empty() ? 0 : output_values[0].size();
    return dataframe_from_columns(columns, row_count);
}

EagerDataFrame execute_sort(const LazyPlanNodePtr& node,
                            const std::function<EagerDataFrame(const LazyPlanNodePtr&)>& execute_plan) {
    if (!node || node->inputs.size() != 1 || !node->inputs[0]) {
        lazy_fail("sort node is malformed");
    }

    EagerDataFrame input = execute_plan(node->inputs[0]);
    std::vector<Column> sort_columns;
    sort_columns.reserve(node->columns.size());
    for (const auto& name : node->columns) {
        sort_columns.push_back(input.column(name));
    }

    std::vector<int64_t> indices(input.row_count());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<int64_t>(i);
    }

    std::stable_sort(indices.begin(), indices.end(), [&](int64_t lhs_row, int64_t rhs_row) {
        for (const auto& column : sort_columns) {
            const LiteralValue lhs = column.value_at(static_cast<std::size_t>(lhs_row));
            const LiteralValue rhs = column.value_at(static_cast<std::size_t>(rhs_row));
            const bool lhs_null = std::holds_alternative<NullType>(lhs);
            const bool rhs_null = std::holds_alternative<NullType>(rhs);

            if (lhs_null && rhs_null) {
                continue;
            }
            if (lhs_null != rhs_null) {
                return !lhs_null;
            }

            const int cmp = compare_literals(lhs, rhs, column.type());
            if (cmp == 0) {
                continue;
            }
            return node->ascending ? (cmp < 0) : (cmp > 0);
        }
        return lhs_row < rhs_row;
    });

    return take_rows(input, indices);
}

std::string make_node_label(const LazyPlanNodePtr& node) {
    std::ostringstream label;
    label << lazy_node_kind_name(node->kind);

    if (!node->path.empty()) {
        label << "\npath=" << node->path;
    }
    if (!node->columns.empty()) {
        label << "\ncols=" << join_vector(node->columns, ",");
    }
    if (node->target_name.has_value()) {
        label << "\nname=" << *node->target_name;
    }
    if (node->join_type.has_value()) {
        label << "\nhow=" << join_type_name(*node->join_type);
    }
    if (node->limit > 0) {
        label << "\nlimit=" << node->limit;
    }
    if (!node->expressions.empty()) {
        label << "\nexprs=" << node->expressions.size();
    }
    if (!node->aggregations.empty()) {
        label << "\naggs=" << node->aggregations.size();
    }

    return label.str();
}

void add_graphviz_subgraph(const LazyPlanNodePtr& node,
                           Agraph_t* graph,
                           std::unordered_map<std::size_t, Agnode_t*>& graph_nodes) {
    if (!node) {
        lazy_fail("cannot explain a null plan node");
    }

    if (graph_nodes.find(node->id) != graph_nodes.end()) {
        return;
    }

    const std::string node_name = "n" + std::to_string(node->id);
    Agnode_t* graph_node = agnode(graph, const_cast<char*>(node_name.c_str()), 1);
    const std::string label = make_node_label(node);
    agset(graph_node, const_cast<char*>("shape"), const_cast<char*>("box"));
    agset(graph_node, const_cast<char*>("label"), const_cast<char*>(label.c_str()));
    graph_nodes.emplace(node->id, graph_node);

    for (const auto& input : node->inputs) {
        add_graphviz_subgraph(input, graph, graph_nodes);
        agedge(graph, graph_nodes.at(input->id), graph_node, nullptr, 1);
    }
}

EagerDataFrame execute_plan_impl(const LazyPlanNodePtr& plan,
                                 std::unordered_map<std::size_t, EagerDataFrame>& cache) {
    if (!plan) {
        lazy_fail("cannot execute a null plan");
    }

    auto cached = cache.find(plan->id);
    if (cached != cache.end()) {
        return cached->second;
    }

    auto recurse = [&](const LazyPlanNodePtr& input) -> EagerDataFrame {
        return execute_plan_impl(input, cache);
    };

    EagerDataFrame result = [&]() -> EagerDataFrame {
        switch (plan->kind) {
            case LazyNodeKind::ScanCsv: {
                EagerDataFrame df = read_csv(plan->path);
                if (!plan->columns.empty()) {
                    df = df.select(plan->columns);
                }
                return df;
            }

            case LazyNodeKind::ScanParquet: {
                EagerDataFrame df = read_parquet(plan->path);
                if (!plan->columns.empty()) {
                    df = df.select(plan->columns);
                }
                return df;
            }

            case LazyNodeKind::SelectColumns:
                return recurse(plan->inputs.at(0)).select(plan->columns);

            case LazyNodeKind::SelectExpressions: {
                EagerDataFrame input = recurse(plan->inputs.at(0));
                std::vector<Column> columns;
                columns.reserve(plan->expressions.size());
                std::unordered_set<std::string> names;

                for (std::size_t i = 0; i < plan->expressions.size(); ++i) {
                    const std::string name = default_expression_name(plan->expressions[i], i);
                    if (!names.insert(name).second) {
                        throw SchemaError("duplicate output column name in select(expressions): " + name);
                    }
                    columns.push_back(evaluate_expression(plan->expressions[i], input, name));
                }

                return dataframe_from_columns(columns, input.row_count());
            }

            case LazyNodeKind::Filter: {
                EagerDataFrame input = recurse(plan->inputs.at(0));
                if (!plan->predicate.has_value()) {
                    throw SchemaError("filter node is missing its predicate");
                }
                Column predicate = evaluate_expression(*plan->predicate, input, "__predicate__");
                return take_rows(input, filter_indices(predicate));
            }

            case LazyNodeKind::WithColumn: {
                EagerDataFrame input = recurse(plan->inputs.at(0));
                if (!plan->target_name.has_value() || plan->expressions.size() != 1) {
                    throw SchemaError("with_column node is malformed");
                }

                Column computed = evaluate_expression(plan->expressions[0], input, *plan->target_name);
                if (computed.size() != input.row_count()) {
                    throw DimensionError("with_column expression produced the wrong number of rows");
                }

                std::vector<Column> columns = dataframe_columns(input);
                bool replaced = false;
                for (auto& column : columns) {
                    if (column.name() == *plan->target_name) {
                        column = computed;
                        replaced = true;
                        break;
                    }
                }
                if (!replaced) {
                    columns.push_back(computed);
                }

                return dataframe_from_columns(columns, input.row_count());
            }

            case LazyNodeKind::GroupBy:
                throw DataFrameError("collect() cannot materialize a bare group_by() without aggregate()");

            case LazyNodeKind::Aggregate:
                return execute_aggregate(plan, recurse);

            case LazyNodeKind::Join:
                return execute_join(plan, recurse);

            case LazyNodeKind::Sort:
                return execute_sort(plan, recurse);

            case LazyNodeKind::Head:
                return recurse(plan->inputs.at(0)).head(plan->limit);

            case LazyNodeKind::SinkCsv:
            case LazyNodeKind::SinkParquet:
                throw DataFrameError("unsupported lazy node kind in collect()");

            case LazyNodeKind::InMemorySource: {
                if (!plan->in_memory_table) {
                    throw SchemaError("in-memory source node is missing its table");
                }

                EagerDataFrame df = from_arrow_table(plan->in_memory_table);
                if (!plan->columns.empty()) {
                    df = df.select(plan->columns);
                }
                return df;
            }
        }

        throw DataFrameError("unknown lazy node kind");
    }();

    cache.emplace(plan->id, result);
    return result;
}

} // namespace

LazyGroupBy::LazyGroupBy(const LazyDataFrame& parent, std::vector<std::string> keys)
    : parent_(&parent), keys_(std::move(keys)) {
    if (!parent_ || !parent_->plan()) {
        lazy_fail("group_by() received a null parent plan");
    }
}

LazyDataFrame LazyGroupBy::aggregate(const std::map<std::string, Expression>& aggs) const {
    auto group_node = make_unary_node(LazyNodeKind::GroupBy, parent_->plan());
    group_node->columns = keys_;

    auto aggregate_node = make_unary_node(LazyNodeKind::Aggregate, group_node);
    aggregate_node->aggregations = aggs;
    return LazyDataFrame(std::move(aggregate_node));
}

LazyDataFrame LazyGroupBy::aggregate(
    const std::vector<std::pair<std::string, std::string>>& aggs) const {
    return aggregate(normalize_named_aggregations(aggs));
}

LazyDataFrame LazyGroupBy::aggregate(
    std::initializer_list<std::pair<std::string, std::string>> aggs) const {
    return aggregate(std::vector<std::pair<std::string, std::string>>(aggs));
}

LazyDataFrame::LazyDataFrame(LazyPlanNodePtr plan)
    : plan_(std::move(plan)) {
    if (!plan_) {
        lazy_fail("constructed with a null plan");
    }
}

LazyDataFrame LazyDataFrame::select(const std::vector<std::string>& columns) const {
    auto node = make_unary_node(LazyNodeKind::SelectColumns, plan_);
    node->columns = columns;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame LazyDataFrame::select(const std::vector<Expression>& expressions) const {
    auto node = make_unary_node(LazyNodeKind::SelectExpressions, plan_);
    node->expressions = expressions;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame LazyDataFrame::filter(const Expression& predicate) const {
    auto node = make_unary_node(LazyNodeKind::Filter, plan_);
    node->predicate = predicate;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame LazyDataFrame::with_column(const std::string& name,
                                         const Expression& expr) const {
    auto node = make_unary_node(LazyNodeKind::WithColumn, plan_);
    node->target_name = name;
    node->expressions = {expr};
    return LazyDataFrame(std::move(node));
}

LazyGroupBy LazyDataFrame::group_by(const std::vector<std::string>& keys) const {
    return LazyGroupBy(*this, keys);
}

LazyDataFrame LazyDataFrame::join(const LazyDataFrame& other,
                                  const std::vector<std::string>& on,
                                  JoinType how) const {
    if (!other.plan_) {
        lazy_fail("join() received a null right-hand plan");
    }

    auto node = make_node(LazyNodeKind::Join);
    node->inputs = {plan_, other.plan_};
    node->columns = on;
    node->join_type = how;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame LazyDataFrame::join(const LazyDataFrame& other,
                                  const std::vector<std::string>& on,
                                  const std::string& how) const {
    return join(other, on, parse_join_type_or_throw(how));
}

LazyDataFrame LazyDataFrame::sort(const std::vector<std::string>& columns,
                                  bool ascending) const {
    auto node = make_unary_node(LazyNodeKind::Sort, plan_);
    node->columns = columns;
    node->ascending = ascending;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame LazyDataFrame::head(std::size_t n) const {
    auto node = make_unary_node(LazyNodeKind::Head, plan_);
    node->limit = n;
    return LazyDataFrame(std::move(node));
}

EagerDataFrame LazyDataFrame::collect() const {
    QueryOptimizer optimizer;
    LazyPlanNodePtr optimized_plan = optimizer.optimize(plan_);
    std::unordered_map<std::size_t, EagerDataFrame> cache;
    return execute_plan_impl(optimized_plan, cache);
}

void LazyDataFrame::sink_csv(const std::string& path) const {
    collect().write_csv(path);
}

void LazyDataFrame::sink_parquet(const std::string& path) const {
    collect().write_parquet(path);
}

void LazyDataFrame::explain(const std::string& path) const {
    QueryOptimizer optimizer;
    OptimizationReport report = optimizer.optimize_with_report(plan_);
    LazyPlanNodePtr rendered_plan = report.optimized_root ? report.optimized_root : plan_;

    GVC_t* context = gvContext();
    if (!context) {
        lazy_fail("failed to initialize Graphviz context");
    }

    Agraph_t* graph = agopen(const_cast<char*>("lazy_plan"), Agdirected, nullptr);
    if (!graph) {
        gvFreeContext(context);
        lazy_fail("failed to create Graphviz graph");
    }

    std::unordered_map<std::size_t, Agnode_t*> graph_nodes;
    add_graphviz_subgraph(rendered_plan, graph, graph_nodes);

    if (gvLayout(context, graph, "dot") != 0) {
        agclose(graph);
        gvFreeContext(context);
        lazy_fail("Graphviz layout failed");
    }

    if (gvRenderFilename(context, graph, "png", path.c_str()) != 0) {
        gvFreeLayout(context, graph);
        agclose(graph);
        gvFreeContext(context);
        lazy_fail("Graphviz render failed for path: " + path);
    }

    gvFreeLayout(context, graph);
    agclose(graph);
    gvFreeContext(context);
}

LazyPlanNodePtr LazyDataFrame::plan() const {
    return plan_;
}

LazyDataFrame scan_csv(const std::string& path) {
    auto node = make_node(LazyNodeKind::ScanCsv);
    node->path = path;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame scan_parquet(const std::string& path) {
    auto node = make_node(LazyNodeKind::ScanParquet);
    node->path = path;
    return LazyDataFrame(std::move(node));
}

LazyDataFrame lazy_from_arrow_table(std::shared_ptr<arrow::Table> table) {
    if (!table) {
        lazy_fail("cannot create a lazy dataframe from a null Arrow table");
    }

    (void)from_arrow_table(table);

    auto node = make_node(LazyNodeKind::InMemorySource);
    node->in_memory_table = std::move(table);
    return LazyDataFrame(std::move(node));
}
