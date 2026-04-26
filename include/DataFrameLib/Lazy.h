#pragma once

#include <cstddef>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Column.h"
#include "Eager.h"
#include "Expression.h"

class QueryOptimizer;

// ============================================================
// include/DataFrameLib/Lazy.h
// ============================================================
//
// Purpose:
// This header defines the lazy execution API and the logical-plan data
// structures needed for Step 4.
//
// Assignment intent:
// - LazyDataFrame should not execute work immediately.
// - Each operation should append a node to a computation DAG.
// - Leaf nodes represent data sources such as scan_csv / scan_parquet.
// - collect() should later optimize and materialize the graph into an
//   EagerDataFrame.
// - explain(path) should later render the DAG to a PNG using Graphviz or
//   another graph utility.
//
// Design direction for the cpp file:
// - Keep plan nodes immutable after construction.
// - Use shared_ptr for DAG node ownership because multiple downstream nodes
//   may reference the same upstream node.
// - Do not mix eager execution logic into plan-construction methods.
// - collect() should be the first place where actual I/O or expression
//   evaluation happens.

enum class LazyNodeKind {
    ScanCsv,
    ScanParquet,
    InMemorySource,
    SelectColumns,
    SelectExpressions,
    Filter,
    WithColumn,
    GroupBy,
    Aggregate,
    Join,
    Sort,
    Head,
    SinkCsv,
    SinkParquet
};

// A single logical-plan node in the lazy DAG.
//
// Why keep this as a simple struct:
// - Step 4 is mostly about preserving the structure of the query.
// - The optimizer in Step 5 needs a transparent representation that is easy
//   to inspect and rewrite.
// - The executor in collect() can later pattern-match on node kind and use
//   the payload fields below.
struct LazyPlanNode {
    std::size_t id = 0;
    LazyNodeKind kind = LazyNodeKind::ScanCsv;

    // Upstream dependencies.
    // Most nodes have one input, while joins have two.
    std::vector<std::shared_ptr<LazyPlanNode>> inputs;

    // For scan/sink nodes.
    std::string path;
    std::shared_ptr<arrow::Table> in_memory_table;

    // For select(columns), sort(columns), group_by(keys), and join(on).
    std::vector<std::string> columns;

    // For expression-based projections.
    std::vector<Expression> expressions;

    // For filter(predicate).
    std::optional<Expression> predicate;

    // For with_column(name, expr).
    std::optional<std::string> target_name;

    // For grouped aggregate output definitions.
    std::map<std::string, Expression> aggregations;

    // For join semantics.
    std::optional<JoinType> join_type;

    // For sort(columns, ascending).
    bool ascending = true;

    // For head(n).
    std::size_t limit = 0;
};

using LazyPlanNodePtr = std::shared_ptr<LazyPlanNode>;

class LazyDataFrame;

// Group-by helper for lazy mode.
//
// Unlike eager mode, this should not aggregate immediately.
// It only remembers the grouping keys and creates a downstream Aggregate node
// when aggregate(...) is called.
class LazyGroupBy {
public:
    LazyGroupBy(const LazyDataFrame& parent, std::vector<std::string> keys);

    LazyDataFrame aggregate(const std::map<std::string, Expression>& aggs) const;
    LazyDataFrame aggregate(const std::vector<std::pair<std::string, std::string>>& aggs) const;
    LazyDataFrame aggregate(std::initializer_list<std::pair<std::string, std::string>> aggs) const;

private:
    const LazyDataFrame* parent_;
    std::vector<std::string> keys_;
};

class LazyDataFrame {
public:
    explicit LazyDataFrame(LazyPlanNodePtr plan);

    // Plan-building operations.
    // Each of these should create a fresh LazyDataFrame whose root node points
    // at the previous plan as an input.
    LazyDataFrame select(const std::vector<std::string>& columns) const;
    LazyDataFrame select(std::initializer_list<std::string> columns) const {
        return select(std::vector<std::string>(columns));
    }
    LazyDataFrame select(const std::vector<Expression>& expressions) const;
    LazyDataFrame filter(const Expression& predicate) const;
    LazyDataFrame with_column(const std::string& name,
                              const Expression& expr) const;

    LazyGroupBy group_by(const std::vector<std::string>& keys) const;

    LazyDataFrame join(const LazyDataFrame& other,
                       const std::vector<std::string>& on,
                       JoinType how) const;

    // Convenience overload matching the assignment examples that pass join
    // kinds as strings such as "inner" or "left".
    LazyDataFrame join(const LazyDataFrame& other,
                       const std::vector<std::string>& on,
                       const std::string& how) const;

    LazyDataFrame sort(const std::vector<std::string>& columns,
                       bool ascending = true) const;

    LazyDataFrame head(std::size_t n) const;

    // Execution / output boundary.
    // These are the methods that should later cross from "logical plan"
    // into "actual work".
    EagerDataFrame collect() const;
    void sink_csv(const std::string& path) const;
    void sink_parquet(const std::string& path) const;
    void explain(const std::string& path) const;

    // Useful internal/public hook for Step 5.
    // The optimizer and explain() logic will need access to the root node.
    LazyPlanNodePtr plan() const;

private:
    LazyPlanNodePtr plan_;
};

// Source constructors expected by the assignment document.
//
// These should create leaf DAG nodes only. They must not read data yet.
LazyDataFrame scan_csv(const std::string& path);
LazyDataFrame scan_parquet(const std::string& path);
LazyDataFrame lazy_from_arrow_table(std::shared_ptr<arrow::Table> table);

// Detailed behavior expected later in src/Lazy.cpp:
//
// LazyDataFrame::select(columns)
//   - Create a SelectColumns node.
//   - Preserve requested order.
//   - Do not resolve columns eagerly; validation can happen in collect()
//     or during an earlier logical-analysis pass if you add one.
//
// LazyDataFrame::select(expressions)
//   - Create a SelectExpressions node.
//   - Store the exact expression list so the optimizer can inspect it.
//   - Alias information must be preserved inside the expressions.
//
// LazyDataFrame::filter(predicate)
//   - Create a Filter node with one input.
//   - The predicate must stay symbolic until collect().
//
// LazyDataFrame::with_column(name, expr)
//   - Create a WithColumn node that records both the output name and the
//     expression tree.
//
// LazyDataFrame::group_by(keys) / LazyGroupBy::aggregate(aggs)
//   - group_by should only capture keys.
//   - aggregate should create GroupBy and Aggregate nodes in the DAG.
//   - The aggregate map should preserve output column names exactly.
//
// LazyDataFrame::join(...)
//   - Create a Join node with two inputs.
//   - Record join keys and join type for the later executor.
//
// LazyDataFrame::sort(...)
//   - Create a Sort node without materializing row order yet.
//
// LazyDataFrame::head(n)
//   - Create a Head/Limit node.
//   - This is important because Step 5 can later push it down.
//
// LazyDataFrame::collect()
//   - Later steps should:
//     1) clone or view the root plan,
//     2) pass it through QueryOptimizer,
//     3) execute the optimized plan into an EagerDataFrame.
//
// LazyDataFrame::explain(path)
//   - Render the current DAG to a graph image.
//   - The rendered graph should make node kinds and dependencies visible.
//   - When optimization is added, explain() may choose to show either the
//     raw plan or the optimized plan, but that choice should be documented.
//
// sink_csv / sink_parquet
//   - These should behave like collect() followed by eager write helpers,
//     unless you later decide to stream the final stage directly.
