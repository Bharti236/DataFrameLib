#pragma once

#include "DataFrameLib/ArrowUtils.h"
#include "DataFrameLib/Column.h"
#include "DataFrameLib/Eager.h"
#include "DataFrameLib/Errors.h"
#include "DataFrameLib/Expression.h"
#include "DataFrameLib/IO.h"
#include "DataFrameLib/Lazy.h"
#include "DataFrameLib/Optimizer.h"
#include "DataFrameLib/Types.h"

#ifndef ARROW_THROW_NOT_OK
#define ARROW_THROW_NOT_OK(status_expr)                                              \
    do {                                                                             \
        auto _dataframelib_arrow_status = (status_expr);                             \
        if (!_dataframelib_arrow_status.ok()) {                                      \
            throw std::runtime_error(_dataframelib_arrow_status.ToString());         \
        }                                                                            \
    } while (false)
#endif

namespace dataframelib {

using ::Column;
using ::ColumnNotFoundError;
using ::DataFrameError;
using ::DataType;
using ::DimensionError;
using ::EagerDataFrame;
using ::EagerGroupBy;
using ::Expression;
using ::JoinType;
using ::LazyDataFrame;
using ::LazyGroupBy;
using ::LazyNodeKind;
using ::LazyPlanNode;
using ::LazyPlanNodePtr;
using ::LiteralValue;
using ::NullError;
using ::NullType;
using ::OptimizerOptions;
using ::OptimizationPassRecord;
using ::OptimizationReport;
using ::OptimizationRule;
using ::QueryOptimizer;
using ::SchemaError;
using ::TypeError;
using ::alias;
using ::col;
using ::contains;
using ::count;
using ::ends_with;
using ::from_arrow_table;
using ::from_columns;
using ::is_null;
using ::is_not_null;
using ::length;
using ::lit;
using ::max;
using ::mean;
using ::min;
using ::optimization_rule_name;
using ::read_csv;
using ::read_parquet;
using ::scan_csv;
using ::scan_parquet;
using ::starts_with;
using ::sum;
using ::to_arrow_table;
using ::to_lower;
using ::to_upper;
using ::write_csv;
using ::write_parquet;
using ::abs;
using ::operator+;
using ::operator-;
using ::operator*;
using ::operator/;
using ::operator%;
using ::operator==;
using ::operator!=;
using ::operator<;
using ::operator<=;
using ::operator>;
using ::operator>=;
using ::operator&;
using ::operator|;
using ::operator~;

} // namespace dataframelib
