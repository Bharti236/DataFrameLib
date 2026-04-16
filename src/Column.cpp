// This version follows Arrow’s immutable-array model: 
// arrays are built with concrete builders, 
// Array::Slice() is the zero-copy slicing path, and 
// concrete arrays expose IsNull(index) / Value(index) for per-row inspection.

#include "../include/DataFrameLib/Column.h"
#include "../include/DataFrameLib/Errors.h"
#include "../include/DataFrameLib/Types.h"
#include "../include/DataFrameLib/ArrowUtils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool is_orderable(DataType t) {
    return is_numeric(t) || is_string(t);
}

long double to_long_double(const LiteralValue& v) {
    if (std::holds_alternative<int32_t>(v)) return static_cast<long double>(std::get<int32_t>(v));
    if (std::holds_alternative<int64_t>(v)) return static_cast<long double>(std::get<int64_t>(v));
    if (std::holds_alternative<float>(v)) return static_cast<long double>(std::get<float>(v));
    if (std::holds_alternative<double>(v)) return static_cast<long double>(std::get<double>(v));
    throw TypeError("expected numeric literal");
}

LiteralValue from_numeric_result(long double value, DataType target_type) {
    switch (target_type) {
        case DataType::Int32:
            return static_cast<int32_t>(value);
        case DataType::Int64:
            return static_cast<int64_t>(value);
        case DataType::Float32:
            return static_cast<float>(value);
        case DataType::Float64:
            return static_cast<double>(value);
        default:
            throw TypeError("internal numeric result type error");
    }
}

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string to_upper_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::shared_ptr<arrow::Array> build_array_or_throw(const std::vector<LiteralValue>& values,
                                                   DataType target_type,
                                                   const std::string& context) {
    auto result = make_array_from_literals(values, target_type);
    if (!result.ok()) {
        throw SchemaError(context + ": " + result.status().ToString());
    }
    return result.ValueOrDie();
}

LiteralValue read_value_from_array(const std::shared_ptr<arrow::Array>& data,
                                   DataType type,
                                   std::size_t row) {
    if (data->IsNull(static_cast<int64_t>(row))) {
        return NullType{};
    }

    switch (type) {
        case DataType::Int32: {
            auto arr = std::static_pointer_cast<arrow::Int32Array>(data);
            return static_cast<int32_t>(arr->Value(static_cast<int64_t>(row)));
        }
        case DataType::Int64: {
            auto arr = std::static_pointer_cast<arrow::Int64Array>(data);
            return static_cast<int64_t>(arr->Value(static_cast<int64_t>(row)));
        }
        case DataType::Float32: {
            auto arr = std::static_pointer_cast<arrow::FloatArray>(data);
            return static_cast<float>(arr->Value(static_cast<int64_t>(row)));
        }
        case DataType::Float64: {
            auto arr = std::static_pointer_cast<arrow::DoubleArray>(data);
            return static_cast<double>(arr->Value(static_cast<int64_t>(row)));
        }
        case DataType::String: {
            auto arr = std::static_pointer_cast<arrow::StringArray>(data);
            return arr->GetString(static_cast<int64_t>(row));
        }
        case DataType::Boolean: {
            auto arr = std::static_pointer_cast<arrow::BooleanArray>(data);
            return arr->Value(static_cast<int64_t>(row));
        }
    }

    throw SchemaError("unsupported column type");
}

template <typename Predicate>
Column unary_boolean_map(const Column& self,
                         const std::string& op_name,
                         Predicate pred) {
    std::vector<LiteralValue> out;
    out.reserve(self.size());

    for (std::size_t i = 0; i < self.size(); ++i) {
        if (self.is_null(i)) {
            out.emplace_back(NullType{});
        } else {
            out.emplace_back(pred(i));
        }
    }

    return Column(self.name(), DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, op_name));
}

template <typename Func>
Column unary_numeric_map(const Column& self,
                         const std::string& op_name,
                         DataType result_type,
                         Func func) {
    std::vector<LiteralValue> out;
    out.reserve(self.size());

    for (std::size_t i = 0; i < self.size(); ++i) {
        if (self.is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto v = read_value_from_array(self.data(), self.type(), i);
        out.emplace_back(from_numeric_result(func(v), result_type));
    }

    return Column(self.name(), result_type,
                  build_array_or_throw(out, result_type, op_name));
}

template <typename Func>
Column unary_string_map(const Column& self,
                        const std::string& op_name,
                        Func func) {
    std::vector<LiteralValue> out;
    out.reserve(self.size());

    for (std::size_t i = 0; i < self.size(); ++i) {
        if (self.is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto v = read_value_from_array(self.data(), self.type(), i);
        out.emplace_back(func(std::get<std::string>(v)));
    }

    return Column(self.name(), DataType::String,
                  build_array_or_throw(out, DataType::String, op_name));
}

template <typename FuncNumeric, typename FuncString, typename FuncBool>
Column binary_compare_map(const Column& lhs,
                          const Column& rhs,
                          const std::string& op_name,
                          FuncNumeric numeric_cmp,
                          FuncString string_cmp,
                          FuncBool bool_cmp) {
    if (lhs.size() != rhs.size()) {
        throw DimensionError("column size mismatch in " + op_name);
    }

    std::vector<LiteralValue> out;
    out.reserve(lhs.size());

    const bool both_numeric = is_numeric(lhs.type()) && is_numeric(rhs.type());
    const bool both_string = is_string(lhs.type()) && is_string(rhs.type());
    const bool both_bool = is_boolean(lhs.type()) && is_boolean(rhs.type());

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs.is_null(i) || rhs.is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto lv = read_value_from_array(lhs.data(), lhs.type(), i);
        auto rv = read_value_from_array(rhs.data(), rhs.type(), i);

        bool result = false;
        if (both_numeric) {
            result = numeric_cmp(lv, rv);
        } else if (both_string) {
            result = string_cmp(std::get<std::string>(lv), std::get<std::string>(rv));
        } else if (both_bool) {
            result = bool_cmp(std::get<bool>(lv), std::get<bool>(rv));
        } else {
            throw TypeError("invalid comparison '" + op_name + "' between " +
                            type_name(lhs.type()) + " and " + type_name(rhs.type()));
        }

        out.emplace_back(result);
    }

    return Column(lhs.name(), DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, op_name));
}

template <typename Func>
Column binary_numeric_map(const Column& lhs,
                          const Column& rhs,
                          const std::string& op_name,
                          DataType result_type,
                          Func func) {
    if (lhs.size() != rhs.size()) {
        throw DimensionError("column size mismatch in " + op_name);
    }

    std::vector<LiteralValue> out;
    out.reserve(lhs.size());

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs.is_null(i) || rhs.is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto lv = read_value_from_array(lhs.data(), lhs.type(), i);
        auto rv = read_value_from_array(rhs.data(), rhs.type(), i);
        long double value = func(to_long_double(lv), to_long_double(rv));
        out.emplace_back(from_numeric_result(value, result_type));
    }

    return Column(lhs.name(), result_type,
                  build_array_or_throw(out, result_type, op_name));
}

template <typename Func>
Column binary_bool_map(const Column& lhs,
                       const Column& rhs,
                       const std::string& op_name,
                       Func func) {
    if (lhs.size() != rhs.size()) {
        throw DimensionError("column size mismatch in " + op_name);
    }

    std::vector<LiteralValue> out;
    out.reserve(lhs.size());

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lhs.is_null(i) || rhs.is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto lv = read_value_from_array(lhs.data(), lhs.type(), i);
        auto rv = read_value_from_array(rhs.data(), rhs.type(), i);
        out.emplace_back(func(std::get<bool>(lv), std::get<bool>(rv)));
    }

    return Column(lhs.name(), DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, op_name));
}

} // namespace

Column::Column(std::string name,
               DataType type,
               std::shared_ptr<arrow::Array> data)
    : name_(std::move(name)), type_(type), data_(std::move(data)) {
    if (!data_) {
        throw SchemaError("column '" + name_ + "' has null data");
    }

    auto actual = from_arrow_type(data_->type());
    if (actual != type_) {
        throw TypeError("column '" + name_ + "' declared as " + type_name(type_) +
                        " but Arrow array is " + type_name(actual));
    }
}

const std::string& Column::name() const {
    return name_;
}

DataType Column::type() const {
    return type_;
}

const std::shared_ptr<arrow::Array>& Column::data() const {
    return data_;
}

std::size_t Column::size() const {
    return static_cast<std::size_t>(data_->length());
}

std::size_t Column::null_count() const {
    return static_cast<std::size_t>(data_->null_count());
}

bool Column::empty() const {
    return size() == 0;
}

bool Column::is_null(std::size_t row) const {
    if (row >= size()) {
        throw DimensionError("row index out of range");
    }
    return data_->IsNull(static_cast<int64_t>(row));
}

LiteralValue Column::value_at(std::size_t row) const {
    if (row >= size()) {
        throw DimensionError("row index out of range");
    }
    return read_value_from_array(data_, type_, row);
}

Column Column::renamed(const std::string& new_name) const {
    return Column(new_name, type_, data_);
}

Column Column::sliced(std::size_t offset, std::size_t length) const {
    if (offset > size()) {
        throw DimensionError("slice offset out of range");
    }

    std::size_t available = size() - offset;
    std::size_t actual_length = std::min(length, available);

    auto sliced_data = data_->Slice(static_cast<int64_t>(offset),
                                   static_cast<int64_t>(actual_length));
    if (!sliced_data) {
        throw DataFrameError("failed to slice column '" + name_ + "'");
    }

    return Column(name_, type_, sliced_data);
}

Column Column::taken(const std::vector<int64_t>& indices) const {
    std::vector<LiteralValue> out;
    out.reserve(indices.size());

    for (int64_t idx : indices) {
        if (idx < 0 || static_cast<std::size_t>(idx) >= size()) {
            throw DimensionError("take index out of range");
        }
        out.push_back(value_at(static_cast<std::size_t>(idx)));
    }

    return Column(name_, type_, build_array_or_throw(out, type_, "take"));
}

Column Column::casted(DataType target_type) const {
    if (target_type == type_) {
        return *this;
    }

    if (!(is_numeric(type_) && is_numeric(target_type))) {
        throw TypeError("cannot cast " + type_name(type_) + " to " + type_name(target_type));
    }

    auto casted_data = cast_array(data_, target_type);
    return Column(name_, target_type, casted_data);
}

Column Column::apply_abs() const {
    if (!is_numeric(type_)) {
        throw_unary_type_error("abs", type_);
    }

    DataType result_type = result_type_for_unary_abs(type_);

    return unary_numeric_map(*this, "abs", result_type, [result_type](const LiteralValue& v) -> long double {
        if (result_type == DataType::Float32 || result_type == DataType::Float64) {
            return std::fabs(to_long_double(v));
        }

        if (std::holds_alternative<int32_t>(v)) {
            return static_cast<long double>(std::llabs(static_cast<long long>(std::get<int32_t>(v))));
        }
        if (std::holds_alternative<int64_t>(v)) {
            return static_cast<long double>(std::llabs(static_cast<long long>(std::get<int64_t>(v))));
        }
        return std::fabs(to_long_double(v));
    });
}

Column Column::apply_is_null() const {
    return unary_boolean_map(*this, "is_null",
                             [this](std::size_t i) { return this->is_null(i); });
}

Column Column::apply_is_not_null() const {
    return unary_boolean_map(*this, "is_not_null",
                             [this](std::size_t i) { return !this->is_null(i); });
}

Column Column::apply_length() const {
    if (!is_string(type_)) {
        throw_unary_type_error("length", type_);
    }

    DataType result_type = result_type_for_length(type_);

    std::vector<LiteralValue> out;
    out.reserve(size());

    for (std::size_t i = 0; i < size(); ++i) {
        if (is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }

        auto s = std::get<std::string>(read_value_from_array(data_, type_, i));
        if (result_type == DataType::Int32) {
            out.emplace_back(static_cast<int32_t>(s.size()));
        } else if (result_type == DataType::Int64) {
            out.emplace_back(static_cast<int64_t>(s.size()));
        } else {
            throw SchemaError("invalid result type for length");
        }
    }

    return Column(name_, result_type,
                  build_array_or_throw(out, result_type, "length"));
}

Column Column::apply_to_lower() const {
    if (!is_string(type_)) {
        throw_unary_type_error("to_lower", type_);
    }

    return unary_string_map(*this, "to_lower", [](const std::string& s) {
        return to_lower_copy(s);
    });
}

Column Column::apply_to_upper() const {
    if (!is_string(type_)) {
        throw_unary_type_error("to_upper", type_);
    }

    return unary_string_map(*this, "to_upper", [](const std::string& s) {
        return to_upper_copy(s);
    });
}

Column Column::add(const Column& rhs) const {
    if (!can_add(type_, rhs.type_)) {
        throw_type_error("+", type_, rhs.type_);
    }
    DataType result_type = promote_numeric(type_, rhs.type_);
    return binary_numeric_map(*this, rhs, "+", result_type,
                              [](long double a, long double b) { return a + b; });
}

Column Column::subtract(const Column& rhs) const {
    if (!can_subtract(type_, rhs.type_)) {
        throw_type_error("-", type_, rhs.type_);
    }
    DataType result_type = promote_numeric(type_, rhs.type_);
    return binary_numeric_map(*this, rhs, "-", result_type,
                              [](long double a, long double b) { return a - b; });
}

Column Column::multiply(const Column& rhs) const {
    if (!can_multiply(type_, rhs.type_)) {
        throw_type_error("*", type_, rhs.type_);
    }
    DataType result_type = promote_numeric(type_, rhs.type_);
    return binary_numeric_map(*this, rhs, "*", result_type,
                              [](long double a, long double b) { return a * b; });
}

Column Column::divide(const Column& rhs) const {
    if (!can_divide(type_, rhs.type_)) {
        throw_type_error("/", type_, rhs.type_);
    }
    DataType result_type = promote_numeric(type_, rhs.type_);

    return binary_numeric_map(*this, rhs, "/", result_type,
                              [](long double a, long double b) {
                                  if (b == 0.0L) {
                                      throw DataFrameError("division by zero");
                                  }
                                  return a / b;
                              });
}

Column Column::modulo(const Column& rhs) const {
    if (!can_modulo(type_, rhs.type_)) {
        throw_type_error("%", type_, rhs.type_);
    }

    DataType result_type = promote_numeric(type_, rhs.type_);

    return binary_numeric_map(*this, rhs, "%", result_type,
                              [](long double a, long double b) {
                                  auto ia = static_cast<long long>(a);
                                  auto ib = static_cast<long long>(b);
                                  if (ib == 0) {
                                      throw DataFrameError("modulo by zero");
                                  }
                                  return static_cast<long double>(ia % ib);
                              });
}

Column Column::equal(const Column& rhs) const {
    if (!can_compare(type_, rhs.type_)) {
        throw_type_error("==", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, "==",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) == to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a == b;
        },
        [](bool a, bool b) {
            return a == b;
        });
}

Column Column::not_equal(const Column& rhs) const {
    if (!can_compare(type_, rhs.type_)) {
        throw_type_error("!=", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, "!=",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) != to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a != b;
        },
        [](bool a, bool b) {
            return a != b;
        });
}

Column Column::less_than(const Column& rhs) const {
    if (!(can_compare(type_, rhs.type_) && is_orderable(type_) && is_orderable(rhs.type_))) {
        throw_type_error("<", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, "<",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) < to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a < b;
        },
        [](bool, bool) {
            throw TypeError("ordering comparison not supported for boolean columns");
            return false;
        });
}

Column Column::less_equal(const Column& rhs) const {
    if (!(can_compare(type_, rhs.type_) && is_orderable(type_) && is_orderable(rhs.type_))) {
        throw_type_error("<=", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, "<=",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) <= to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a <= b;
        },
        [](bool, bool) {
            throw TypeError("ordering comparison not supported for boolean columns");
            return false;
        });
}

Column Column::greater_than(const Column& rhs) const {
    if (!(can_compare(type_, rhs.type_) && is_orderable(type_) && is_orderable(rhs.type_))) {
        throw_type_error(">", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, ">",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) > to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a > b;
        },
        [](bool, bool) {
            throw TypeError("ordering comparison not supported for boolean columns");
            return false;
        });
}

Column Column::greater_equal(const Column& rhs) const {
    if (!(can_compare(type_, rhs.type_) && is_orderable(type_) && is_orderable(rhs.type_))) {
        throw_type_error(">=", type_, rhs.type_);
    }

    return binary_compare_map(
        *this, rhs, ">=",
        [](const LiteralValue& a, const LiteralValue& b) {
            return to_long_double(a) >= to_long_double(b);
        },
        [](const std::string& a, const std::string& b) {
            return a >= b;
        },
        [](bool, bool) {
            throw TypeError("ordering comparison not supported for boolean columns");
            return false;
        });
}

Column Column::logical_and(const Column& rhs) const {
    if (!can_apply_boolean_op(type_, rhs.type_)) {
        throw_type_error("&&", type_, rhs.type_);
    }

    return binary_bool_map(*this, rhs, "&&",
                           [](bool a, bool b) { return a && b; });
}

Column Column::logical_or(const Column& rhs) const {
    if (!can_apply_boolean_op(type_, rhs.type_)) {
        throw_type_error("||", type_, rhs.type_);
    }

    return binary_bool_map(*this, rhs, "||",
                           [](bool a, bool b) { return a || b; });
}

Column Column::logical_not() const {
    if (!is_boolean(type_)) {
        throw_unary_type_error("not", type_);
    }

    std::vector<LiteralValue> out;
    out.reserve(size());

    for (std::size_t i = 0; i < size(); ++i) {
        if (is_null(i)) {
            out.emplace_back(NullType{});
        } else {
            auto v = std::get<bool>(read_value_from_array(data_, type_, i));
            out.emplace_back(!v);
        }
    }

    return Column(name_, DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, "not"));
}

Column Column::contains(const std::string& needle) const {
    if (!is_string(type_)) {
        throw_unary_type_error("contains", type_);
    }

    std::vector<LiteralValue> out;
    out.reserve(size());

    for (std::size_t i = 0; i < size(); ++i) {
        if (is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }
        auto s = std::get<std::string>(read_value_from_array(data_, type_, i));
        out.emplace_back(s.find(needle) != std::string::npos);
    }

    return Column(name_, DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, "contains"));
}

Column Column::starts_with(const std::string& prefix) const {
    if (!is_string(type_)) {
        throw_unary_type_error("starts_with", type_);
    }

    std::vector<LiteralValue> out;
    out.reserve(size());

    for (std::size_t i = 0; i < size(); ++i) {
        if (is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }
        auto s = std::get<std::string>(read_value_from_array(data_, type_, i));
        out.emplace_back(s.rfind(prefix, 0) == 0);
    }

    return Column(name_, DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, "starts_with"));
}

Column Column::ends_with(const std::string& suffix) const {
    if (!is_string(type_)) {
        throw_unary_type_error("ends_with", type_);
    }

    std::vector<LiteralValue> out;
    out.reserve(size());

    for (std::size_t i = 0; i < size(); ++i) {
        if (is_null(i)) {
            out.emplace_back(NullType{});
            continue;
        }
        auto s = std::get<std::string>(read_value_from_array(data_, type_, i));
        bool ok = s.size() >= suffix.size() &&
                  s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
        out.emplace_back(ok);
    }

    return Column(name_, DataType::Boolean,
                  build_array_or_throw(out, DataType::Boolean, "ends_with"));
}
