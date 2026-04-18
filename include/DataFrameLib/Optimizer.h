#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Lazy.h"

// ============================================================
// include/DataFrameLib/Optimizer.h
// ============================================================
//
// Purpose:
// This header defines the complete Step 5 optimizer contract for the lazy
// query engine.
//
// Design goals:
// - Keep optimization rule-based, predictable, and easy to justify in the viva.
// - Preserve correctness by rewriting the logical DAG into an equivalent DAG.
// - Keep the original plan reusable by cloning/rebuilding nodes instead of
//   mutating shared DAG nodes in place.
// - Expose enough diagnostics that collect() and explain() can later show
//   what the optimizer actually changed.
//
// Why this file is intentionally detailed:
// - The assignment explicitly grades the optimizer separately.
// - The report must explain every implemented optimization, its correctness,
//   an example, and the expected performance gain.
// - A heavily-commented header gives src/Optimizer.cpp a concrete checklist
//   and keeps the public API/documentation aligned with the implementation.

// The required rule families from the assignment.
//
// The optimizer runs these as separate passes so that:
// - each rule remains easy to test in isolation,
// - the final report can discuss each transformation independently,
// - and a fixpoint loop can re-run earlier rules after later rewrites expose
//   new opportunities.
enum class OptimizationRule {
    PredicatePushdown,
    ProjectionPushdown,
    ConstantFolding,
    ExpressionSimplification,
    LimitPushdown
};

// Stable string form used by logs, optimizer reports, explain() output, and
// report-writing helpers.
std::string optimization_rule_name(OptimizationRule rule);

// Configuration switches for one optimizer instance.
//
// The defaults enable all required assignment optimizations so a plain
// QueryOptimizer() object represents the "full Step 5" behavior.
struct OptimizerOptions {
    // Enables moving filters closer to scan nodes and beneath safe unary
    // operators such as projections that preserve row identity.
    bool enable_predicate_pushdown = true;

    // Enables backward collection of required columns so scan nodes can read
    // only the schema subset needed by downstream operators.
    bool enable_projection_pushdown = true;

    // Enables compile-time reduction of constant-only expression subtrees,
    // such as 2 + 3 or lower("ABC"), before execution starts.
    bool enable_constant_folding = true;

    // Enables algebraic cleanups like x * 1 -> x, x + 0 -> x,
    // and filter(true) elimination once expressions are simplified.
    bool enable_expression_simplification = true;

    // Enables safe movement/merging of head/limit nodes so row caps happen as
    // early as semantics allow.
    bool enable_limit_pushdown = true;

    // When true, every rewritten plan is checked for structural and type
    // consistency before the next pass begins. This is especially useful while
    // bringing src/Optimizer.cpp online.
    bool validate_after_each_pass = true;

    // When true, optimize_with_report() fills pass-by-pass diagnostics that
    // explain() and README material can later surface.
    bool collect_diagnostics = true;

    // Maximum number of fixpoint iterations. A small hard cap prevents buggy
    // rule interactions from looping forever while still allowing multiple
    // passes to expose new opportunities.
    std::size_t max_iterations = 8;
};

// One pass record inside the optimizer report.
//
// This is the unit of traceability for "what happened during optimization".
// A single rule may appear more than once because the optimizer is expected to
// run to a fixpoint rather than exactly once.
struct OptimizationPassRecord {
    // Which rule family this record corresponds to.
    OptimizationRule rule = OptimizationRule::PredicatePushdown;

    // Which fixpoint iteration this pass belonged to.
    std::size_t iteration = 0;

    // Whether the rule changed the plan during this pass.
    bool changed = false;

    // Number of local rewrites performed inside the pass.
    std::size_t rewrite_count = 0;

    // Human-readable explanation suitable for debugging or README excerpts.
    std::string summary;
};

// Complete diagnostic payload returned by optimize_with_report().
//
// The optimized root is the actual output used by collect()/explain().
// Everything else exists so the optimizer can justify its work and so the
// report can demonstrate that a transformation really happened.
struct OptimizationReport {
    // The original root passed into the optimizer.
    LazyPlanNodePtr original_root;

    // The final optimized root after all enabled passes finish.
    LazyPlanNodePtr optimized_root;

    // Number of unique DAG nodes before rewriting starts.
    std::size_t nodes_before = 0;

    // Number of unique DAG nodes after rewriting completes.
    std::size_t nodes_after = 0;

    // Number of fixpoint iterations that actually ran.
    std::size_t iterations = 0;

    // Pass-by-pass trace in execution order.
    std::vector<OptimizationPassRecord> passes;

    // Free-form notes for assumptions, validation warnings, or "rule skipped"
    // explanations when a transformation is intentionally conservative.
    std::vector<std::string> notes;
};

// Rule-based optimizer for lazy logical plans.
//
// Expected integration points:
// - LazyDataFrame::collect() should call optimize(plan()) before execution.
// - LazyDataFrame::explain(path) can call optimize_with_report(plan()) and
//   render the optimized DAG instead of the raw one.
// - Benchmarks can instantiate the optimizer with selected rules disabled to
//   compare baseline vs optimized performance.
class QueryOptimizer {
public:
    // Captures the pass configuration for the lifetime of this optimizer.
    explicit QueryOptimizer(OptimizerOptions options = {});

    // Fast public entry point used by collect().
    //
    // The returned plan must be logically equivalent to the input plan but
    // potentially cheaper to execute. The input root remains reusable.
    LazyPlanNodePtr optimize(const LazyPlanNodePtr& root) const;

    // Rich entry point used by explain(), tests, and report generation.
    //
    // This returns both the optimized plan and a full trace of which passes
    // ran, which ones changed the plan, and how many rewrites happened.
    OptimizationReport optimize_with_report(const LazyPlanNodePtr& root) const;

    // Exposes the currently configured pass switches without requiring the
    // caller to mirror them externally.
    const OptimizerOptions& options() const;

    // Lightweight rule-status query useful for tests and explain() output.
    bool is_rule_enabled(OptimizationRule rule) const;

private:
    // Tracks the columns that a downstream consumer truly needs from a
    // subplan. Projection pushdown walks this information upward toward scan
    // nodes.
    struct ColumnRequirement {
        // When true, the optimizer must preserve the full schema because the
        // consumer depends on all columns or the exact requirement is unknown.
        bool requires_all_columns = false;

        // Concrete column names needed from the subplan when the requirement
        // can be expressed precisely.
        std::unordered_set<std::string> names;
    };

    // Generic "rewrite result" container for plan-level rules.
    struct PlanRewriteResult {
        // Root of the rewritten subtree.
        LazyPlanNodePtr root;

        // Whether the rule changed anything semantically relevant in the
        // returned subtree.
        bool changed = false;

        // Number of local rewrites performed while producing `root`.
        std::size_t rewrite_count = 0;
    };

    // Generic "rewrite result" container for expression-level rules.
    struct ExpressionRewriteResult {
        // Rewritten expression when the rule can produce one.
        std::optional<Expression> expression;

        // Whether any fold/simplification happened.
        bool changed = false;
    };

    // Per-run caches and diagnostics shared across rule passes.
    //
    // Keeping this state in a local context rather than on QueryOptimizer
    // itself makes optimize() thread-safe and keeps repeated optimizer runs
    // independent.
    struct OptimizationContext {
        // Memoizes cloned nodes so DAG sharing is preserved when the input plan
        // is copied before rewriting.
        std::unordered_map<std::size_t, LazyPlanNodePtr> clone_cache;

        // Memoizes inferred output schemas for already-visited subplans.
        std::unordered_map<std::size_t, std::shared_ptr<arrow::Schema>> schema_cache;

        // Memoizes column requirements during projection analysis.
        std::unordered_map<std::size_t, ColumnRequirement> requirement_cache;

        // Memoizes lightweight scan schemas so repeated pushdown passes do not
        // reopen the same source metadata unnecessarily.
        std::unordered_map<std::string, std::shared_ptr<arrow::Schema>> scan_schema_cache;

        // Accumulates pass-by-pass diagnostics for optimize_with_report().
        OptimizationReport report;
    };

    // Returns the pass order for one fixpoint iteration.
    //
    // The expected order is:
    // 1) fold constants,
    // 2) simplify expressions,
    // 3) push predicates,
    // 4) push projections,
    // 5) push/merge limits.
    std::vector<OptimizationRule> enabled_rules() const;

    // Runs all enabled rules repeatedly until no rule changes the plan or the
    // configured iteration cap is reached.
    OptimizationReport run_fixpoint_pipeline(const LazyPlanNodePtr& root) const;

    // Dispatches one rule family to its dedicated implementation.
    PlanRewriteResult apply_rule(OptimizationRule rule,
                                 const LazyPlanNodePtr& root,
                                 OptimizationContext& context) const;

    // Appends a human-readable pass record to the report.
    void record_pass(OptimizationRule rule,
                     std::size_t iteration,
                     const PlanRewriteResult& result,
                     OptimizationContext& context) const;

    // Verifies structural invariants after a rewrite.
    //
    // Examples:
    // - non-null roots,
    // - valid input arity for each node kind,
    // - required payload fields present,
    // - expression types remain valid against inferred schemas.
    void validate_plan(const LazyPlanNodePtr& root, OptimizationContext& context) const;

    // Counts unique DAG nodes for diagnostics and benchmarking reports.
    std::size_t count_unique_nodes(const LazyPlanNodePtr& root) const;

    // Clones the incoming DAG while preserving shared subplans.
    //
    // This lets the optimizer rebuild nodes freely without mutating plans that
    // earlier LazyDataFrame objects might still hold.
    LazyPlanNodePtr clone_plan(const LazyPlanNodePtr& root, OptimizationContext& context) const;

    // Copies a single node payload without recursively cloning its inputs.
    //
    // Rule implementations use this to rebuild just the parts of the tree that
    // need to change while leaving untouched subtrees shared.
    LazyPlanNodePtr clone_node_without_inputs(const LazyPlanNodePtr& node) const;

    // Applies constant folding to all expressions reachable from the plan.
    PlanRewriteResult apply_constant_folding(const LazyPlanNodePtr& root,
                                             OptimizationContext& context) const;

    // Applies algebraic cleanups and trivial node eliminations after constant
    // folding has exposed simpler forms.
    PlanRewriteResult apply_expression_simplification(const LazyPlanNodePtr& root,
                                                      OptimizationContext& context) const;

    // Pushes filters toward scans and beneath safe intermediate operators.
    PlanRewriteResult apply_predicate_pushdown(const LazyPlanNodePtr& root,
                                               OptimizationContext& context) const;

    // Pushes required-column information upstream so sources and projections
    // can prune unused data early.
    PlanRewriteResult apply_projection_pushdown(const LazyPlanNodePtr& root,
                                                OptimizationContext& context) const;

    // Pushes or merges head/limit operators where row-order semantics allow.
    PlanRewriteResult apply_limit_pushdown(const LazyPlanNodePtr& root,
                                           OptimizationContext& context) const;

    // Recursive helper used by constant folding and expression simplification.
    //
    // This walk rewrites expressions attached to select/filter/with_column/
    // aggregate nodes without changing plan shape unless a node becomes
    // trivial enough to remove entirely.
    PlanRewriteResult rewrite_plan_expressions(const LazyPlanNodePtr& root,
                                               OptimizationContext& context,
                                               bool fold_constants,
                                               bool simplify_expressions) const;

    // Recursive helper used by predicate pushdown. It rebuilds the plan
    // bottom-up and attempts local filter movements whenever a Filter node is
    // encountered.
    PlanRewriteResult rewrite_predicates(const LazyPlanNodePtr& root,
                                         OptimizationContext& context) const;

    // Recursive helper used by limit pushdown. It handles cases such as
    // merging adjacent head() nodes and moving limits beneath row-preserving
    // projections.
    PlanRewriteResult rewrite_limits(const LazyPlanNodePtr& root,
                                     OptimizationContext& context) const;

    // Recursive helper used by projection pushdown.
    //
    // `downstream_requirement` describes the columns needed by the parent of
    // `root`; this function propagates that requirement to children.
    PlanRewriteResult rewrite_projections(const LazyPlanNodePtr& root,
                                          const ColumnRequirement& downstream_requirement,
                                          OptimizationContext& context) const;

    // Attempts to move a filter below a unary node such as:
    // - select(columns),
    // - select(expressions) when the predicate can be rewritten in terms of
    //   the child columns,
    // - with_column when the predicate does not depend on the new column.
    PlanRewriteResult push_filter_through_unary_node(const LazyPlanNodePtr& filter_node,
                                                     const LazyPlanNodePtr& child_node,
                                                     OptimizationContext& context) const;

    // Attempts to split a filter above a join into:
    // - predicates that depend only on the left input,
    // - predicates that depend only on the right input,
    // - residual predicates that must stay above the join.
    PlanRewriteResult push_filter_through_join(const LazyPlanNodePtr& filter_node,
                                               const LazyPlanNodePtr& join_node,
                                               OptimizationContext& context) const;

    // Attempts to move a limit beneath a row-preserving unary node.
    //
    // Safe examples:
    // - Head above SelectColumns,
    // - Head above SelectExpressions that keep one output row per input row,
    // - Head above WithColumn.
    PlanRewriteResult push_limit_through_unary_node(const LazyPlanNodePtr& limit_node,
                                                    const LazyPlanNodePtr& child_node,
                                                    OptimizationContext& context) const;

    // Collapses head(head(n1), n2) into head(min(n1, n2)).
    PlanRewriteResult merge_adjacent_limits(const LazyPlanNodePtr& limit_node,
                                            const LazyPlanNodePtr& child_node) const;

    // Computes the columns required from a subplan by a downstream consumer.
    ColumnRequirement derive_child_requirement(const LazyPlanNodePtr& node,
                                               const ColumnRequirement& downstream_requirement,
                                               std::size_t child_index,
                                               OptimizationContext& context) const;

    // Returns the initial requirement for the root plan: all final output
    // columns must survive optimization.
    ColumnRequirement root_requirement(const LazyPlanNodePtr& root,
                                       OptimizationContext& context) const;

    // Applies projection annotations directly to scan nodes.
    //
    // The current LazyPlanNode shape does not yet have a dedicated
    // scan-projection field, so the implementation is expected to reuse
    // LazyPlanNode::columns on ScanCsv/ScanParquet nodes as the list of
    // required source columns.
    void annotate_scan_projection(LazyPlanNode& scan_node,
                                  const ColumnRequirement& requirement,
                                  OptimizationContext& context) const;

    // Infers the output schema of a plan node after optimization.
    //
    // Schema knowledge is required for:
    // - validating rewritten expressions,
    // - deciding whether a predicate references only left or right join input,
    // - building the correct required-column sets for projection pushdown.
    std::shared_ptr<arrow::Schema> output_schema(const LazyPlanNodePtr& root,
                                                 OptimizationContext& context) const;

    // Loads just enough schema information from a scan node to support
    // pushdown. For Parquet this should come from file metadata; for CSV this
    // may require the same strict Arrow schema inference used by read_csv().
    std::shared_ptr<arrow::Schema> scan_schema(const LazyPlanNodePtr& scan_node,
                                               OptimizationContext& context) const;

    // Builds the post-join schema, including right-side name disambiguation
    // rules that must match execution behavior.
    std::shared_ptr<arrow::Schema> join_output_schema(const LazyPlanNodePtr& join_node,
                                                      OptimizationContext& context) const;

    // Builds the grouped aggregate output schema:
    // group keys first, aggregation outputs second.
    std::shared_ptr<arrow::Schema> aggregate_output_schema(const LazyPlanNodePtr& aggregate_node,
                                                           OptimizationContext& context) const;

    // Rewrites one expression subtree, optionally folding constants and/or
    // simplifying algebraic identities.
    ExpressionRewriteResult rewrite_expression(const Expression& expression,
                                               const arrow::Schema* input_schema,
                                               bool fold_constants,
                                               bool simplify_expressions) const;

    // Evaluates a constant-only expression subtree to a literal so it can be
    // replaced by lit(...).
    std::optional<LiteralValue> evaluate_constant_expression(const Expression& expression) const;

    // Applies algebraic identities to a single already-rewritten expression.
    ExpressionRewriteResult simplify_expression(const Expression& expression) const;

    // Splits a boolean predicate connected by AND into independent conjuncts.
    // Predicate pushdown uses this so each conjunct can be routed separately.
    std::vector<Expression> split_conjunctive_predicate(const Expression& predicate) const;

    // Rebuilds a predicate from conjuncts after pushdown analysis.
    Expression combine_conjunctive_predicates(const std::vector<Expression>& conjuncts) const;

    // Checks whether an expression depends only on a specific set of column
    // names. This is the core safety test for join-side predicate pushdown.
    bool expression_uses_only_columns(const Expression& expression,
                                      const std::unordered_set<std::string>& allowed_columns) const;

    // Rewrites an expression through a projection binding map.
    //
    // Example:
    // - projection defines `salary_usd = col("salary") * lit(83.0)`
    // - downstream filter references `salary_usd > lit(1000.0)`
    // This helper substitutes the projected definition back into the filter so
    // the predicate can potentially move below the projection.
    std::optional<Expression> rewrite_expression_with_bindings(
        const Expression& expression,
        const std::map<std::string, Expression>& bindings) const;

    // Extracts the output-name -> defining-expression mapping for
    // select(expressions) and with_column nodes when such a mapping exists.
    std::map<std::string, Expression> projection_bindings(const LazyPlanNodePtr& node) const;

    // Preserves alias semantics after a rewrite.
    //
    // If the original expression was aliased, the rewritten expression must
    // keep that visible output name.
    Expression preserve_alias(const Expression& original,
                              const Expression& rewritten) const;

    // Fixed configuration for this optimizer instance.
    OptimizerOptions options_;
};
