#pragma once

#include <arrow/api.h>
#include <arrow/table.h>

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Column.h"
#include "Expression.h"
#include "Types.h"

class EagerDataFrame;

enum class JoinType {
    Inner,
    Left,
    Right,
    Full
};

class EagerGroupBy {
public:
    EagerGroupBy(const EagerDataFrame& parent, std::vector<std::string> keys);

    EagerDataFrame aggregate(const std::map<std::string, Expression>& aggs) const;

private:
    const EagerDataFrame* parent_;
    std::vector<std::string> keys_;
};

class EagerDataFrame {
public:
    explicit EagerDataFrame(std::shared_ptr<arrow::Table> table);

    // Optional convenience constructor.
    EagerDataFrame(const std::map<std::string, Column>& columns);

    static EagerDataFrame read_csv(const std::string& path);
    static EagerDataFrame read_parquet(const std::string& path);

    std::size_t row_count() const;
    std::size_t column_count() const;

    const arrow::Schema& schema() const;
    std::shared_ptr<arrow::Table> to_arrow_table() const;

    bool has_column(const std::string& name) const;
    Column column(const std::string& name) const;

    EagerDataFrame select(const std::vector<std::string>& columns) const;
    EagerDataFrame select(const std::vector<Expression>& expressions) const;

    EagerDataFrame filter(const Expression& predicate) const;

    EagerDataFrame with_column(const std::string& name,
                               const Expression& expr) const;

    EagerGroupBy group_by(const std::vector<std::string>& keys) const;

    EagerDataFrame join(const EagerDataFrame& other,
                        const std::vector<std::string>& on,
                        JoinType how) const;

    EagerDataFrame sort(const std::vector<std::string>& columns,
                        bool ascending = true) const;

    EagerDataFrame head(std::size_t n) const;

    void write_csv(const std::string& path) const;
    void write_parquet(const std::string& path) const;

    std::string to_string() const;
    void validate() const;

private:
    std::shared_ptr<arrow::Table> table_;
    std::shared_ptr<arrow::Schema> schema_;

    std::size_t column_index(const std::string& name) const;
    static DataType arrow_type_to_datatype(const std::shared_ptr<arrow::DataType>& type);
    static void validate_table(const std::shared_ptr<arrow::Table>& table);
};

// // EagerDataFrame class

// #include <arrow/api.h>
// #include <arrow/table.h>
// #include <memory>
// #include <string>
// #include <vector>

// #include <map>
// #include "Column.h"
// #include "Types.h"
// #include "ArrowUtils.h"
// #include "Expression.h"

// // namespace DataFrameLib {

// // class EagerDataFrame {
// // public:
// //     // Wrap the Arrow Table
// //     explicit EagerDataFrame(std::shared_ptr<arrow::Table> table) : table_(std::move(table)) {}

// //     // 3.1 I/O Functions 
// //     static EagerDataFrame read_csv(const std::string& path);
// //     static EagerDataFrame read_parquet(const std::string& path);
    
// //     // 3.2 Operations
// //     EagerDataFrame select(const std::vector<std::string>& columns);
// //     // Add others: filter, with_column, sort, etc.

// //     void write_csv(const std::string& path);

// // private:
// //     std::shared_ptr<arrow::Table> table_;
// // };

// // } // namespace DataFrameLib

// // ============================================================
// // include/DataFrameLib/Eager.h
// // ============================================================

// // Purpose:
// // This header defines the immediate-execution DataFrame class.
// // EagerDataFrame owns fully materialized Arrow-backed column data.
// // Every operation returns a new materialized EagerDataFrame.

// // IMPORTANT:
// // - This class is the eager counterpart to LazyDataFrame.
// // - It must execute now, not later.
// // - It must preserve strict type safety.
// // - It must preserve null semantics.
// // - It must never mutate existing objects in place.

// // Recommended internal representation:

// class EagerDataFrame {
// private:
//     std::shared_ptr<arrow::Table> table_;
//     arrow::Schema schema_;   // or a similar logical schema wrapper
// };

// // The table should remain immutable once stored.
// // Each operation should construct and return a new EagerDataFrame.

// // ------------------------------------------------------------
// // Constructors / factories
// // ------------------------------------------------------------

// EagerDataFrame(std::shared_ptr<arrow::Table> table);
// //   Constructs a validated eager frame from an Arrow table.
// //   This should verify:
// //   - every column has a supported type,
// //   - column names are unique,
// //   - all columns have equal length,
// //   - the logical schema matches the allowed strict types.

// EagerDataFrame(const std::map<std::string, Column>& columns);
// //   Optional convenience constructor if you already have strict Column
// //   objects and want to build a frame directly.

// std::size_t row_count() const;
// //   Returns the number of rows currently stored.

// std::size_t column_count() const;
// //   Returns the number of columns currently stored.

// const arrow::Schema& schema() const;
// //   Returns the logical schema of the frame.

// bool has_column(const std::string& name) const;
// //   Checks whether a column exists.

// Column column(const std::string& name) const;
// //   Returns a copy or view of the named column for inspection.

// // ------------------------------------------------------------
// // select(...)
// // ------------------------------------------------------------

// EagerDataFrame select(const std::vector<std::string>& columns) const;
// //   Keeps only the named columns, in the same order as requested.
// //   This is a purely materialized projection.
// //   If any requested column is missing, throw immediately.

// EagerDataFrame select(const std::vector<Expression>& expressions) const;
// //   Evaluates each expression immediately against the current frame.
// //   The result becomes a new materialized frame.
// //   This form is important because select may include:
// //   - renamed columns,
// //   - literals,
// //   - arithmetic expressions,
// //   - boolean expressions,
// //   - string expressions,
// //   - aggregate expressions only when semantically valid.

// // Detailed behavior:
// // - Each expression is type-checked before evaluation.
// // - Aliases become output column names.
// // - If an expression has no alias, use a sensible derived name.
// // - If the same output name appears twice, throw SchemaError.
// // - All expressions are evaluated on the current materialized data.

// // ------------------------------------------------------------
// // filter(...)
// // ------------------------------------------------------------

// // EagerDataFrame filter(const Expression& predicate) const;
// //   Keeps only rows where predicate evaluates to true.
// //   Predicate must evaluate to a Boolean column.
// //   Rows where the predicate is null should be excluded unless you
// //   explicitly define three-valued logic differently.
// //   For this assignment, the safe approach is:
// //   - true  -> keep row
// //   - false -> drop row
// //   - null  -> drop row

// // Detailed behavior:
// // - Predicate is validated against the current schema.
// // - Predicate must not produce non-boolean output.
// // - The row count after filtering may be smaller.
// // - Column order and types must remain unchanged.

// // ------------------------------------------------------------
// // with_column(...)
// // ------------------------------------------------------------

// EagerDataFrame with_column(const std::string& name,
//                            const Expression& expr) const;
// //   Evaluates the expression immediately and adds the result as a new
// //   column, or replaces an existing column with the same name.

// // Detailed behavior:
// // - The expression must be evaluated against the current frame.
// // - The result length must exactly match the frame row count.
// // - If replacing an existing column, the new column must keep its new type.
// // - The frame remains immutable; this function returns a new frame.
// // - If the expression references missing columns, throw immediately.

// // Common uses:
// // - computed numeric columns,
// // - boolean flags,
// // - string transformations,
// // - null checks,
// // - aliasing results into a named column.

// // ------------------------------------------------------------
// // group_by(...)
// // ------------------------------------------------------------

// // Grouping in eager mode should be represented by a small helper object,
// // not by executing aggregation immediately.

// // Suggested pattern:

// class EagerGroupBy {
// public:
//     EagerGroupBy(const EagerDataFrame& parent,
//                  std::vector<std::string> keys);

//     EagerDataFrame aggregate(const std::map<std::string, Expression>& aggs) const;
// };

// EagerGroupBy group_by(const std::vector<std::string>& keys) const;
// //   Creates a grouped view over the current frame.
// //   This should validate that all key columns exist.

// // Detailed behavior:
// // - Keys define the grouping columns.
// // - Rows with equal key values belong to the same group.
// // - Nulls in key columns should be handled consistently and documented.
// //   A safe choice is to treat null key values as equal to each other only if
// //   your grouping semantics explicitly allow that; otherwise group them as
// //   separate null groups only if required by the tests.
// // - The group_by call itself should not yet collapse rows.

// // ------------------------------------------------------------
// // aggregate(...)
// // ------------------------------------------------------------

// EagerDataFrame aggregate(const std::map<std::string, Expression>& aggs) const;
// //   This belongs on the grouping helper, not directly on the base frame.
// //   It produces one output row per group.

// // For each entry in the aggregation map:
// //   key   -> output column name
// //   value -> aggregation expression such as col("salary").mean()

// // Detailed behavior:
// // - Every aggregation expression must be valid on the grouped input.
// // - Aggregations like sum/mean/count/min/max must operate over each group.
// // - count() should count non-null values.
// // - mean() should ignore nulls when computing the average.
// // - If a group has no non-null values for a metric, the result should
// //   follow a well-defined null policy.
// // - Output schema should include:
// //   - group keys first,
// //   - then aggregation outputs.

// // Important implementation note:
// // - Aggregation is easiest if you first build a group index map from key
// //   tuples to row indices, then evaluate each aggregation per group.

// // ------------------------------------------------------------
// // join(...)
// // ------------------------------------------------------------

// EagerDataFrame join(const EagerDataFrame& other,
//                     const std::vector<std::string>& on,
//                     JoinType how) const;
// //   Joins two materialized frames eagerly.

// // Suggested JoinType enum:
// //   Inner, Left, Right, Full

// // Detailed behavior:
// // - The join keys must exist in both frames.
// // - Join types must be implemented consistently.
// // - Output schema should combine columns from both sides.
// // - Duplicate non-key column names must be handled explicitly.
// //   A common strategy is suffixing or disallowing ambiguous overlaps.
// // - Null join-key behavior must be defined clearly and applied consistently.
// // - For an inner join, only matching key rows appear.
// // - For a left join, all left rows appear.
// // - For a right join, all right rows appear.
// // - For a full join, all rows from both sides appear.

// // Implementation hint:
// // - Build hash maps for key tuples.
// // - Use Arrow-backed data access for row extraction.
// // - Materialize the result into a new table/columns.

// // ------------------------------------------------------------
// // sort(...)
// // ------------------------------------------------------------

// EagerDataFrame sort(const std::vector<std::string>& columns,
//                     bool ascending = true) const;
// //   Returns a new frame with rows ordered by the given columns.

// // Detailed behavior:
// // - All sort columns must exist.
// // - Sorting should be stable if practical.
// // - The row order changes; column contents do not.
// // - Null ordering should be defined consistently.
// //   A good default is to place nulls last for ascending and first for
// //   descending, but the key point is consistency.

// // If multiple sort columns are supplied:
// // - compare the first column,
// // - if equal, compare the second,
// // - and so on.

// // ------------------------------------------------------------
// // head(...)
// // ------------------------------------------------------------

// EagerDataFrame head(std::size_t n) const;
// //   Returns the first n rows of the frame.
// //   If n exceeds the row count, return all rows.
// //   This should be a simple slice operation.

// // ------------------------------------------------------------
// // Additional utility methods that are very useful in tests
// // ------------------------------------------------------------

// void write_csv(const std::string& path) const;
// void write_parquet(const std::string& path) const;
// //   Instance methods that forward to the free functions in IO.h.
// //   These are convenient because the assignment examples call write methods
// //   directly on the DataFrame.

// std::string to_string() const;
// //   Useful for debugging and unit tests.

// void validate() const;
// //   Checks schema consistency and internal invariants.