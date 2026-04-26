#include "../include/DataFrameLib/Optimizer.h"

#include "../include/DataFrameLib/ArrowUtils.h"
#include "../include/DataFrameLib/Errors.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

[[noreturn]] void optimizer_fail(const std::string& message) {
    throw DataFrameError("QueryOptimizer: " + message);
}

std::size_t next_optimizer_node_id() {
    static std::atomic<std::size_t> next_id{static_cast<std::size_t>(1) << 60U};
    return next_id++;
}

LazyPlanNodePtr make_optimizer_node(LazyNodeKind kind) {
    auto node = std::make_shared<LazyPlanNode>();
    node->id = next_optimizer_node_id();
    node->kind = kind;
    return node;
}

std::string lowercase_ascii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::vector<std::shared_ptr<Expression>> clone_expression_children(
    const std::vector<Expression>& children) {
    std::vector<std::shared_ptr<Expression>> out;
    out.reserve(children.size());
    for (const auto& child : children) {
        out.push_back(std::make_shared<Expression>(child));
    }
    return out;
}

Expression rebuild_expression(const Expression& expression,
                              const std::vector<Expression>& rewritten_children) {
    if (expression.kind() == ExprKind::Alias) {
        if (rewritten_children.size() != 1) {
            throw TypeError("alias expression must have exactly one child");
        }
        return ::alias(rewritten_children[0], expression.alias_name());
    }

    return Expression(expression.kind(),
                      expression.op(),
                      expression.name(),
                      expression.literal(),
                      clone_expression_children(rewritten_children));
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

std::string unique_name(std::unordered_set<std::string>& seen, const std::string& base) {
    std::string candidate = base;
    std::size_t suffix = 1;
    while (!seen.insert(candidate).second) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

bool is_null_literal_value(const LiteralValue& value) {
    return std::holds_alternative<NullType>(value);
}

std::optional<DataType> literal_type(const LiteralValue& value) {
    if (std::holds_alternative<NullType>(value)) {
        return std::nullopt;
    }
    if (std::holds_alternative<int32_t>(value)) {
        return DataType::Int32;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return DataType::Int64;
    }
    if (std::holds_alternative<float>(value)) {
        return DataType::Float32;
    }
    if (std::holds_alternative<double>(value)) {
        return DataType::Float64;
    }
    if (std::holds_alternative<std::string>(value)) {
        return DataType::String;
    }
    if (std::holds_alternative<bool>(value)) {
        return DataType::Boolean;
    }
    return std::nullopt;
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
        case DataType::Int32:
            return static_cast<int32_t>(value);
        case DataType::Int64:
            return static_cast<int64_t>(value);
        case DataType::Float32:
            return static_cast<float>(value);
        case DataType::Float64:
            return static_cast<double>(value);
        default:
            throw TypeError("invalid numeric target type");
    }
}

bool literal_equals_zero(const LiteralValue& value) {
    if (std::holds_alternative<int32_t>(value)) {
        return std::get<int32_t>(value) == 0;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value) == 0;
    }
    if (std::holds_alternative<float>(value)) {
        return std::get<float>(value) == 0.0f;
    }
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) == 0.0;
    }
    return false;
}

bool literal_equals_one(const LiteralValue& value) {
    if (std::holds_alternative<int32_t>(value)) {
        return std::get<int32_t>(value) == 1;
    }
    if (std::holds_alternative<int64_t>(value)) {
        return std::get<int64_t>(value) == 1;
    }
    if (std::holds_alternative<float>(value)) {
        return std::get<float>(value) == 1.0f;
    }
    if (std::holds_alternative<double>(value)) {
        return std::get<double>(value) == 1.0;
    }
    return false;
}

const Expression& strip_alias(const Expression& expression) {
    const Expression* current = &expression;
    while (current->kind() == ExprKind::Alias &&
           current->children().size() == 1 &&
           current->children()[0]) {
        current = current->children()[0].get();
    }
    return *current;
}

bool is_zero_literal_expression(const Expression& expression) {
    const Expression& stripped = strip_alias(expression);
    return stripped.kind() == ExprKind::Literal &&
           stripped.literal().has_value() &&
           literal_equals_zero(*stripped.literal());
}

bool is_one_literal_expression(const Expression& expression) {
    const Expression& stripped = strip_alias(expression);
    return stripped.kind() == ExprKind::Literal &&
           stripped.literal().has_value() &&
           literal_equals_one(*stripped.literal());
}

bool is_boolean_literal_expression(const Expression& expression, bool value) {
    const Expression& stripped = strip_alias(expression);
    return stripped.kind() == ExprKind::Literal &&
           stripped.literal().has_value() &&
           std::holds_alternative<bool>(*stripped.literal()) &&
           std::get<bool>(*stripped.literal()) == value;
}

bool is_self_column_assignment(const std::string& target_name, const Expression& expression) {
    const Expression& stripped = strip_alias(expression);
    return stripped.kind() == ExprKind::ColumnRef &&
           stripped.name().has_value() &&
           *stripped.name() == target_name;
}

bool schema_has_field(const arrow::Schema& schema, const std::string& name) {
    return schema.GetFieldIndex(name) >= 0;
}

std::vector<std::string> schema_field_names(const arrow::Schema& schema) {
    std::vector<std::string> names;
    names.reserve(schema.num_fields());
    for (const auto& field : schema.fields()) {
        if (field) {
            names.push_back(field->name());
        }
    }
    return names;
}

Expression make_and_expression(const Expression& lhs, const Expression& rhs) {
    return lhs & rhs;
}

} // namespace

std::string optimization_rule_name(OptimizationRule rule) {
    switch (rule) {
        case OptimizationRule::PredicatePushdown:
            return "PredicatePushdown";
        case OptimizationRule::ProjectionPushdown:
            return "ProjectionPushdown";
        case OptimizationRule::ConstantFolding:
            return "ConstantFolding";
        case OptimizationRule::ExpressionSimplification:
            return "ExpressionSimplification";
        case OptimizationRule::LimitPushdown:
            return "LimitPushdown";
    }

    return "UnknownOptimizationRule";
}

QueryOptimizer::QueryOptimizer(OptimizerOptions options)
    : options_(std::move(options)) {}

LazyPlanNodePtr QueryOptimizer::optimize(const LazyPlanNodePtr& root) const {
    return optimize_with_report(root).optimized_root;
}

OptimizationReport QueryOptimizer::optimize_with_report(const LazyPlanNodePtr& root) const {
    return run_fixpoint_pipeline(root);
}

const OptimizerOptions& QueryOptimizer::options() const {
    return options_;
}

bool QueryOptimizer::is_rule_enabled(OptimizationRule rule) const {
    switch (rule) {
        case OptimizationRule::PredicatePushdown:
            return options_.enable_predicate_pushdown;
        case OptimizationRule::ProjectionPushdown:
            return options_.enable_projection_pushdown;
        case OptimizationRule::ConstantFolding:
            return options_.enable_constant_folding;
        case OptimizationRule::ExpressionSimplification:
            return options_.enable_expression_simplification;
        case OptimizationRule::LimitPushdown:
            return options_.enable_limit_pushdown;
    }

    return false;
}

std::vector<OptimizationRule> QueryOptimizer::enabled_rules() const {
    std::vector<OptimizationRule> rules;
    if (options_.enable_constant_folding) {
        rules.push_back(OptimizationRule::ConstantFolding);
    }
    if (options_.enable_expression_simplification) {
        rules.push_back(OptimizationRule::ExpressionSimplification);
    }
    if (options_.enable_predicate_pushdown) {
        rules.push_back(OptimizationRule::PredicatePushdown);
    }
    if (options_.enable_projection_pushdown) {
        rules.push_back(OptimizationRule::ProjectionPushdown);
    }
    if (options_.enable_limit_pushdown) {
        rules.push_back(OptimizationRule::LimitPushdown);
    }
    return rules;
}

OptimizationReport QueryOptimizer::run_fixpoint_pipeline(const LazyPlanNodePtr& root) const {
    if (!root) {
        optimizer_fail("cannot optimize a null plan");
    }

    OptimizationContext context;
    context.report.original_root = root;
    context.report.nodes_before = count_unique_nodes(root);

    LazyPlanNodePtr current = clone_plan(root, context);
    context.clone_cache.clear();

    if (options_.validate_after_each_pass) {
        context.schema_cache.clear();
        context.requirement_cache.clear();
        validate_plan(current, context);
    }

    const std::vector<OptimizationRule> rules = enabled_rules();
    if (rules.empty()) {
        context.report.optimized_root = current;
        context.report.nodes_after = count_unique_nodes(current);
        return context.report;
    }

    std::size_t iterations_run = 0;
    for (std::size_t iteration = 1; iteration <= options_.max_iterations; ++iteration) {
        bool changed_this_iteration = false;
        iterations_run = iteration;

        for (OptimizationRule rule : rules) {
            context.schema_cache.clear();
            context.requirement_cache.clear();

            PlanRewriteResult result = apply_rule(rule, current, context);
            if (!result.root) {
                result.root = current;
            }

            if (options_.validate_after_each_pass) {
                context.schema_cache.clear();
                context.requirement_cache.clear();
                validate_plan(result.root, context);
            }

            record_pass(rule, iteration, result, context);
            current = result.root;
            changed_this_iteration = changed_this_iteration || result.changed;
        }

        if (!changed_this_iteration) {
            break;
        }

        if (iteration == options_.max_iterations && options_.collect_diagnostics) {
            context.report.notes.push_back(
                "optimizer reached the maximum iteration cap before proving a fixpoint");
        }
    }

    context.report.iterations = iterations_run;
    context.report.optimized_root = current;
    context.report.nodes_after = count_unique_nodes(current);
    return context.report;
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_rule(OptimizationRule rule,
                                                             const LazyPlanNodePtr& root,
                                                             OptimizationContext& context) const {
    switch (rule) {
        case OptimizationRule::PredicatePushdown:
            return apply_predicate_pushdown(root, context);
        case OptimizationRule::ProjectionPushdown:
            return apply_projection_pushdown(root, context);
        case OptimizationRule::ConstantFolding:
            return apply_constant_folding(root, context);
        case OptimizationRule::ExpressionSimplification:
            return apply_expression_simplification(root, context);
        case OptimizationRule::LimitPushdown:
            return apply_limit_pushdown(root, context);
    }

    optimizer_fail("unknown optimization rule");
}

void QueryOptimizer::record_pass(OptimizationRule rule,
                                 std::size_t iteration,
                                 const PlanRewriteResult& result,
                                 OptimizationContext& context) const {
    if (!options_.collect_diagnostics) {
        return;
    }

    OptimizationPassRecord record;
    record.rule = rule;
    record.iteration = iteration;
    record.changed = result.changed;
    record.rewrite_count = result.rewrite_count;

    std::ostringstream summary;
    summary << optimization_rule_name(rule) << " iteration " << iteration
            << (result.changed ? " changed " : " kept ")
            << "the plan";
    if (result.rewrite_count > 0) {
        summary << " (" << result.rewrite_count << " rewrite"
                << (result.rewrite_count == 1 ? "" : "s") << ")";
    }
    record.summary = summary.str();

    context.report.passes.push_back(std::move(record));
}

void QueryOptimizer::validate_plan(const LazyPlanNodePtr& root,
                                   OptimizationContext& context) const {
    if (!root) {
        optimizer_fail("validation received a null root");
    }

    std::unordered_set<std::size_t> visited;
    std::function<void(const LazyPlanNodePtr&)> validate =
        [&](const LazyPlanNodePtr& node) {
            if (!node || !visited.insert(node->id).second) {
                return;
            }

            for (const auto& input : node->inputs) {
                if (!input) {
                    optimizer_fail("plan contains a null input edge");
                }
                validate(input);
            }

            auto require_unary = [&](const std::string& label) {
                if (node->inputs.size() != 1 || !node->inputs[0]) {
                    optimizer_fail(label + " node must have exactly one input");
                }
            };

            switch (node->kind) {
                case LazyNodeKind::ScanCsv:
                case LazyNodeKind::ScanParquet:
                    if (node->path.empty()) {
                        optimizer_fail("scan node has an empty path");
                    }
                    (void)output_schema(node, context);
                    break;

                case LazyNodeKind::SelectColumns: {
                    require_unary("SelectColumns");
                    auto schema = output_schema(node->inputs[0], context);
                    std::unordered_set<std::string> seen;
                    for (const auto& name : node->columns) {
                        if (!schema_has_field(*schema, name)) {
                            throw ColumnNotFoundError("missing selected column: " + name);
                        }
                        if (!seen.insert(name).second) {
                            throw SchemaError("duplicate selected column: " + name);
                        }
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::SelectExpressions: {
                    require_unary("SelectExpressions");
                    auto schema = output_schema(node->inputs[0], context);
                    std::unordered_set<std::string> names;
                    for (std::size_t i = 0; i < node->expressions.size(); ++i) {
                        (void)node->expressions[i].infer_type(*schema);
                        const std::string name = default_expression_name(node->expressions[i], i);
                        if (!names.insert(name).second) {
                            throw SchemaError("duplicate output column name in select(expressions): " + name);
                        }
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::Filter: {
                    require_unary("Filter");
                    if (!node->predicate.has_value()) {
                        throw SchemaError("filter node is missing its predicate");
                    }
                    auto schema = output_schema(node->inputs[0], context);
                    const DataType type = node->predicate->infer_type(*schema);
                    if (type != DataType::Boolean) {
                        throw TypeError("filter predicate must be boolean");
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::WithColumn: {
                    require_unary("WithColumn");
                    if (!node->target_name.has_value() || node->expressions.size() != 1) {
                        throw SchemaError("with_column node is malformed");
                    }
                    auto schema = output_schema(node->inputs[0], context);
                    (void)node->expressions[0].infer_type(*schema);
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::GroupBy: {
                    require_unary("GroupBy");
                    auto schema = output_schema(node->inputs[0], context);
                    for (const auto& key : node->columns) {
                        if (!schema_has_field(*schema, key)) {
                            throw ColumnNotFoundError("missing group-by key: " + key);
                        }
                    }
                    break;
                }

                case LazyNodeKind::Aggregate: {
                    require_unary("Aggregate");
                    if (!node->inputs[0] || node->inputs[0]->kind != LazyNodeKind::GroupBy) {
                        throw SchemaError("aggregate node must consume a GroupBy node");
                    }
                    auto group_node = node->inputs[0];
                    auto input_schema = output_schema(group_node->inputs[0], context);
                    for (const auto& [name, expr] : node->aggregations) {
                        (void)name;
                        (void)expr.infer_type(*input_schema);
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::Join: {
                    if (node->inputs.size() != 2 || !node->inputs[0] || !node->inputs[1]) {
                        optimizer_fail("join node must have two inputs");
                    }
                    if (!node->join_type.has_value()) {
                        throw SchemaError("join node is missing its join type");
                    }
                    auto left_schema = output_schema(node->inputs[0], context);
                    auto right_schema = output_schema(node->inputs[1], context);
                    for (const auto& key : node->columns) {
                        auto left_field = left_schema->GetFieldByName(key);
                        auto right_field = right_schema->GetFieldByName(key);
                        if (!left_field) {
                            throw ColumnNotFoundError("missing left join key: " + key);
                        }
                        if (!right_field) {
                            throw ColumnNotFoundError("missing right join key: " + key);
                        }
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::Sort: {
                    require_unary("Sort");
                    auto schema = output_schema(node->inputs[0], context);
                    for (const auto& name : node->columns) {
                        if (!schema_has_field(*schema, name)) {
                            throw ColumnNotFoundError("missing sort column: " + name);
                        }
                    }
                    (void)output_schema(node, context);
                    break;
                }

                case LazyNodeKind::Head:
                    require_unary("Head");
                    (void)output_schema(node, context);
                    break;

                case LazyNodeKind::SinkCsv:
                case LazyNodeKind::SinkParquet:
                case LazyNodeKind::InMemorySource:
                    break;
            }
        };

    validate(root);
}

std::size_t QueryOptimizer::count_unique_nodes(const LazyPlanNodePtr& root) const {
    if (!root) {
        return 0;
    }

    std::unordered_set<std::size_t> visited;
    std::function<void(const LazyPlanNodePtr&)> walk = [&](const LazyPlanNodePtr& node) {
        if (!node || !visited.insert(node->id).second) {
            return;
        }
        for (const auto& input : node->inputs) {
            walk(input);
        }
    };

    walk(root);
    return visited.size();
}

LazyPlanNodePtr QueryOptimizer::clone_plan(const LazyPlanNodePtr& root,
                                           OptimizationContext& context) const {
    if (!root) {
        return nullptr;
    }

    auto cached = context.clone_cache.find(root->id);
    if (cached != context.clone_cache.end()) {
        return cached->second;
    }

    LazyPlanNodePtr cloned = clone_node_without_inputs(root);
    context.clone_cache.emplace(root->id, cloned);

    for (const auto& input : root->inputs) {
        cloned->inputs.push_back(clone_plan(input, context));
    }

    return cloned;
}

LazyPlanNodePtr QueryOptimizer::clone_node_without_inputs(const LazyPlanNodePtr& node) const {
    if (!node) {
        return nullptr;
    }

    auto cloned = make_optimizer_node(node->kind);
    cloned->path = node->path;
    cloned->in_memory_table = node->in_memory_table;
    cloned->columns = node->columns;
    cloned->expressions = node->expressions;
    cloned->predicate = node->predicate;
    cloned->target_name = node->target_name;
    cloned->aggregations = node->aggregations;
    cloned->join_type = node->join_type;
    cloned->ascending = node->ascending;
    cloned->limit = node->limit;
    return cloned;
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_constant_folding(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    return rewrite_plan_expressions(root, context, true, false);
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_expression_simplification(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    return rewrite_plan_expressions(root, context, false, true);
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_predicate_pushdown(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    return rewrite_predicates(root, context);
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_projection_pushdown(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    return rewrite_projections(root, root_requirement(root, context), context);
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::apply_limit_pushdown(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    return rewrite_limits(root, context);
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::rewrite_plan_expressions(
    const LazyPlanNodePtr& root,
    OptimizationContext& context,
    bool fold_constants,
    bool simplify_expressions) const {
    if (!root) {
        return {};
    }

    bool changed = false;
    std::size_t rewrite_count = 0;

    std::vector<LazyPlanNodePtr> rewritten_inputs;
    rewritten_inputs.reserve(root->inputs.size());
    for (const auto& input : root->inputs) {
        PlanRewriteResult child = rewrite_plan_expressions(input, context, fold_constants, simplify_expressions);
        rewritten_inputs.push_back(child.root);
        changed = changed || child.changed;
        rewrite_count += child.rewrite_count;
    }

    LazyPlanNodePtr rebuilt = clone_node_without_inputs(root);
    rebuilt->inputs = std::move(rewritten_inputs);

    const arrow::Schema* input_schema = nullptr;
    std::shared_ptr<arrow::Schema> schema_holder;
    if (!rebuilt->inputs.empty()) {
        if (rebuilt->kind == LazyNodeKind::Aggregate &&
            rebuilt->inputs[0] &&
            rebuilt->inputs[0]->kind == LazyNodeKind::GroupBy &&
            !rebuilt->inputs[0]->inputs.empty()) {
            schema_holder = output_schema(rebuilt->inputs[0]->inputs[0], context);
        } else {
            schema_holder = output_schema(rebuilt->inputs[0], context);
        }
        input_schema = schema_holder.get();
    }

    auto rewrite_expr = [&](const Expression& expression) -> Expression {
        ExpressionRewriteResult expr_result = rewrite_expression(
            expression,
            input_schema,
            fold_constants,
            simplify_expressions
        );

        if (!expr_result.expression.has_value()) {
            return expression;
        }

        changed = changed || expr_result.changed;
        if (expr_result.changed) {
            ++rewrite_count;
        }
        return *expr_result.expression;
    };

    switch (rebuilt->kind) {
        case LazyNodeKind::SelectExpressions:
            for (auto& expression : rebuilt->expressions) {
                expression = rewrite_expr(expression);
            }
            break;

        case LazyNodeKind::Filter:
            if (rebuilt->predicate.has_value()) {
                rebuilt->predicate = rewrite_expr(*rebuilt->predicate);
            }
            if (rebuilt->predicate.has_value() &&
                is_boolean_literal_expression(*rebuilt->predicate, true) &&
                rebuilt->inputs.size() == 1) {
                return {rebuilt->inputs[0], true, rewrite_count + 1};
            }
            break;

        case LazyNodeKind::WithColumn:
            if (!rebuilt->expressions.empty()) {
                rebuilt->expressions[0] = rewrite_expr(rebuilt->expressions[0]);
            }
            if (rebuilt->target_name.has_value() &&
                rebuilt->expressions.size() == 1 &&
                is_self_column_assignment(*rebuilt->target_name, rebuilt->expressions[0]) &&
                rebuilt->inputs.size() == 1) {
                return {rebuilt->inputs[0], true, rewrite_count + 1};
            }
            break;

        case LazyNodeKind::Aggregate:
            for (auto& [name, expression] : rebuilt->aggregations) {
                (void)name;
                expression = rewrite_expr(expression);
            }
            break;

        default:
            break;
    }

    return {rebuilt, changed, rewrite_count};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::rewrite_predicates(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    if (!root) {
        return {};
    }

    bool changed = false;
    std::size_t rewrite_count = 0;

    std::vector<LazyPlanNodePtr> rewritten_inputs;
    rewritten_inputs.reserve(root->inputs.size());
    for (const auto& input : root->inputs) {
        PlanRewriteResult child = rewrite_predicates(input, context);
        rewritten_inputs.push_back(child.root);
        changed = changed || child.changed;
        rewrite_count += child.rewrite_count;
    }

    LazyPlanNodePtr rebuilt = clone_node_without_inputs(root);
    rebuilt->inputs = std::move(rewritten_inputs);

    if (rebuilt->kind != LazyNodeKind::Filter ||
        rebuilt->inputs.size() != 1 ||
        !rebuilt->predicate.has_value()) {
        return {rebuilt, changed, rewrite_count};
    }

    LazyPlanNodePtr child = rebuilt->inputs[0];
    if (child && child->kind == LazyNodeKind::Filter && child->predicate.has_value() &&
        child->inputs.size() == 1 && child->inputs[0]) {
        rebuilt->predicate = combine_conjunctive_predicates(
            { *child->predicate, *rebuilt->predicate }
        );
        rebuilt->inputs[0] = child->inputs[0];
        child = rebuilt->inputs[0];
        changed = true;
        ++rewrite_count;
    }

    PlanRewriteResult local = {rebuilt, false, 0};
    if (child) {
        switch (child->kind) {
            case LazyNodeKind::SelectColumns:
            case LazyNodeKind::SelectExpressions:
            case LazyNodeKind::WithColumn:
            case LazyNodeKind::Sort:
                local = push_filter_through_unary_node(rebuilt, child, context);
                break;

            case LazyNodeKind::Join:
                local = push_filter_through_join(rebuilt, child, context);
                break;

            default:
                break;
        }
    }

    if (local.root) {
        rebuilt = local.root;
        changed = changed || local.changed;
        rewrite_count += local.rewrite_count;
    }

    return {rebuilt, changed, rewrite_count};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::rewrite_limits(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    if (!root) {
        return {};
    }

    bool changed = false;
    std::size_t rewrite_count = 0;

    std::vector<LazyPlanNodePtr> rewritten_inputs;
    rewritten_inputs.reserve(root->inputs.size());
    for (const auto& input : root->inputs) {
        PlanRewriteResult child = rewrite_limits(input, context);
        rewritten_inputs.push_back(child.root);
        changed = changed || child.changed;
        rewrite_count += child.rewrite_count;
    }

    LazyPlanNodePtr rebuilt = clone_node_without_inputs(root);
    rebuilt->inputs = std::move(rewritten_inputs);

    if (rebuilt->kind != LazyNodeKind::Head ||
        rebuilt->inputs.size() != 1 ||
        !rebuilt->inputs[0]) {
        return {rebuilt, changed, rewrite_count};
    }

    LazyPlanNodePtr child = rebuilt->inputs[0];

    if (child->kind == LazyNodeKind::Head) {
        PlanRewriteResult merged = merge_adjacent_limits(rebuilt, child);
        rebuilt = merged.root;
        changed = changed || merged.changed;
        rewrite_count += merged.rewrite_count;
        child = rebuilt->inputs[0];
    }

    if (child &&
        (child->kind == LazyNodeKind::SelectColumns ||
         child->kind == LazyNodeKind::SelectExpressions ||
         child->kind == LazyNodeKind::WithColumn)) {
        PlanRewriteResult pushed = push_limit_through_unary_node(rebuilt, child, context);
        rebuilt = pushed.root;
        changed = changed || pushed.changed;
        rewrite_count += pushed.rewrite_count;
    }

    return {rebuilt, changed, rewrite_count};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::rewrite_projections(
    const LazyPlanNodePtr& root,
    const ColumnRequirement& downstream_requirement,
    OptimizationContext& context) const {
    if (!root) {
        return {};
    }

    bool changed = false;
    std::size_t rewrite_count = 0;

    std::vector<LazyPlanNodePtr> rewritten_inputs;
    rewritten_inputs.reserve(root->inputs.size());
    for (std::size_t i = 0; i < root->inputs.size(); ++i) {
        ColumnRequirement child_requirement =
            derive_child_requirement(root, downstream_requirement, i, context);
        PlanRewriteResult child = rewrite_projections(root->inputs[i], child_requirement, context);
        rewritten_inputs.push_back(child.root);
        changed = changed || child.changed;
        rewrite_count += child.rewrite_count;
    }

    LazyPlanNodePtr rebuilt = clone_node_without_inputs(root);
    rebuilt->inputs = std::move(rewritten_inputs);

    if (rebuilt->kind == LazyNodeKind::ScanCsv || rebuilt->kind == LazyNodeKind::ScanParquet) {
        const std::vector<std::string> before = rebuilt->columns;
        annotate_scan_projection(*rebuilt, downstream_requirement, context);
        if (rebuilt->columns != before) {
            changed = true;
            ++rewrite_count;
        }
        return {rebuilt, changed, rewrite_count};
    }

    if (rebuilt->kind == LazyNodeKind::SelectColumns && !downstream_requirement.requires_all_columns) {
        std::vector<std::string> kept;
        kept.reserve(rebuilt->columns.size());
        for (const auto& name : rebuilt->columns) {
            if (downstream_requirement.names.count(name) > 0) {
                kept.push_back(name);
            }
        }
        if (kept != rebuilt->columns) {
            rebuilt->columns = std::move(kept);
            changed = true;
            ++rewrite_count;
        }
    }

    if (rebuilt->kind == LazyNodeKind::WithColumn &&
        rebuilt->target_name.has_value() &&
        !downstream_requirement.requires_all_columns &&
        downstream_requirement.names.count(*rebuilt->target_name) == 0 &&
        rebuilt->inputs.size() == 1) {
        return {rebuilt->inputs[0], true, rewrite_count + 1};
    }

    if (rebuilt->kind == LazyNodeKind::Aggregate &&
        !downstream_requirement.requires_all_columns &&
        rebuilt->inputs.size() == 1 &&
        rebuilt->inputs[0] &&
        rebuilt->inputs[0]->kind == LazyNodeKind::GroupBy) {
        std::map<std::string, Expression> kept_aggs;
        for (const auto& [name, expr] : rebuilt->aggregations) {
            if (downstream_requirement.names.count(name) > 0) {
                kept_aggs.emplace(name, expr);
            }
        }
        if (kept_aggs.size() != rebuilt->aggregations.size()) {
            rebuilt->aggregations = std::move(kept_aggs);
            changed = true;
            ++rewrite_count;
        }
    }

    return {rebuilt, changed, rewrite_count};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::push_filter_through_unary_node(
    const LazyPlanNodePtr& filter_node,
    const LazyPlanNodePtr& child_node,
    OptimizationContext& context) const {
    if (!filter_node || !child_node || !filter_node->predicate.has_value()) {
        return {filter_node, false, 0};
    }

    if (child_node->inputs.size() != 1 || !child_node->inputs[0]) {
        return {filter_node, false, 0};
    }

    std::optional<Expression> rewritten_predicate;
    switch (child_node->kind) {
        case LazyNodeKind::Sort:
            rewritten_predicate = filter_node->predicate;
            break;

        case LazyNodeKind::SelectColumns: {
            std::unordered_set<std::string> allowed(child_node->columns.begin(), child_node->columns.end());
            if (expression_uses_only_columns(*filter_node->predicate, allowed)) {
                rewritten_predicate = *filter_node->predicate;
            }
            break;
        }

        case LazyNodeKind::SelectExpressions:
        case LazyNodeKind::WithColumn: {
            const auto bindings = projection_bindings(child_node);
            rewritten_predicate = rewrite_expression_with_bindings(*filter_node->predicate, bindings);
            break;
        }

        default:
            return {filter_node, false, 0};
    }

    if (!rewritten_predicate.has_value()) {
        return {filter_node, false, 0};
    }

    auto child_schema = output_schema(child_node->inputs[0], context);
    const DataType predicate_type = rewritten_predicate->infer_type(*child_schema);
    if (predicate_type != DataType::Boolean) {
        return {filter_node, false, 0};
    }

    LazyPlanNodePtr pushed_filter = clone_node_without_inputs(filter_node);
    pushed_filter->predicate = *rewritten_predicate;
    pushed_filter->inputs = {child_node->inputs[0]};

    LazyPlanNodePtr rebuilt_child = clone_node_without_inputs(child_node);
    rebuilt_child->inputs = {pushed_filter};

    return {rebuilt_child, true, 1};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::push_filter_through_join(
    const LazyPlanNodePtr& filter_node,
    const LazyPlanNodePtr& join_node,
    OptimizationContext& context) const {
    if (!filter_node || !join_node || !filter_node->predicate.has_value()) {
        return {filter_node, false, 0};
    }
    if (join_node->inputs.size() != 2 || !join_node->inputs[0] || !join_node->inputs[1]) {
        return {filter_node, false, 0};
    }

    auto left_schema = output_schema(join_node->inputs[0], context);
    auto right_schema = output_schema(join_node->inputs[1], context);
    std::unordered_set<std::string> join_keys(join_node->columns.begin(), join_node->columns.end());

    std::unordered_set<std::string> left_output_names;
    std::unordered_set<std::string> right_output_names;
    std::map<std::string, Expression> right_bindings;

    std::unordered_set<std::string> seen_names;
    for (const auto& field : left_schema->fields()) {
        if (!field) {
            continue;
        }
        left_output_names.insert(field->name());
        seen_names.insert(field->name());
    }

    for (const auto& field : right_schema->fields()) {
        if (!field || join_keys.count(field->name()) > 0) {
            continue;
        }

        std::string output_name = field->name();
        if (seen_names.count(output_name) > 0) {
            output_name += "_right";
        }
        output_name = unique_name(seen_names, output_name);

        right_output_names.insert(output_name);
        right_bindings.emplace(output_name, col(field->name()));
    }

    std::vector<Expression> left_conjuncts;
    std::vector<Expression> right_conjuncts;
    std::vector<Expression> residual_conjuncts;

    for (const auto& conjunct : split_conjunctive_predicate(*filter_node->predicate)) {
        if (expression_uses_only_columns(conjunct, left_output_names)) {
            left_conjuncts.push_back(conjunct);
            continue;
        }

        if (expression_uses_only_columns(conjunct, right_output_names)) {
            auto rewritten = rewrite_expression_with_bindings(conjunct, right_bindings);
            if (rewritten.has_value()) {
                right_conjuncts.push_back(*rewritten);
                continue;
            }
        }

        residual_conjuncts.push_back(conjunct);
    }

    if (left_conjuncts.empty() && right_conjuncts.empty()) {
        return {filter_node, false, 0};
    }

    LazyPlanNodePtr left_input = join_node->inputs[0];
    LazyPlanNodePtr right_input = join_node->inputs[1];

    if (!left_conjuncts.empty()) {
        LazyPlanNodePtr left_filter = make_optimizer_node(LazyNodeKind::Filter);
        left_filter->predicate = combine_conjunctive_predicates(left_conjuncts);
        left_filter->inputs = {left_input};
        left_input = left_filter;
    }

    if (!right_conjuncts.empty()) {
        LazyPlanNodePtr right_filter = make_optimizer_node(LazyNodeKind::Filter);
        right_filter->predicate = combine_conjunctive_predicates(right_conjuncts);
        right_filter->inputs = {right_input};
        right_input = right_filter;
    }

    LazyPlanNodePtr rebuilt_join = clone_node_without_inputs(join_node);
    rebuilt_join->inputs = {left_input, right_input};

    if (residual_conjuncts.empty()) {
        return {rebuilt_join, true, left_conjuncts.size() + right_conjuncts.size()};
    }

    LazyPlanNodePtr residual_filter = clone_node_without_inputs(filter_node);
    residual_filter->predicate = combine_conjunctive_predicates(residual_conjuncts);
    residual_filter->inputs = {rebuilt_join};

    return {residual_filter, true, left_conjuncts.size() + right_conjuncts.size()};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::push_limit_through_unary_node(
    const LazyPlanNodePtr& limit_node,
    const LazyPlanNodePtr& child_node,
    OptimizationContext& context) const {
    (void)context;

    if (!limit_node || !child_node || child_node->inputs.size() != 1 || !child_node->inputs[0]) {
        return {limit_node, false, 0};
    }

    LazyPlanNodePtr pushed_limit = clone_node_without_inputs(limit_node);
    pushed_limit->inputs = {child_node->inputs[0]};

    LazyPlanNodePtr rebuilt_child = clone_node_without_inputs(child_node);
    rebuilt_child->inputs = {pushed_limit};

    return {rebuilt_child, true, 1};
}

QueryOptimizer::PlanRewriteResult QueryOptimizer::merge_adjacent_limits(
    const LazyPlanNodePtr& limit_node,
    const LazyPlanNodePtr& child_node) const {
    if (!limit_node || !child_node || child_node->kind != LazyNodeKind::Head ||
        child_node->inputs.size() != 1 || !child_node->inputs[0]) {
        return {limit_node, false, 0};
    }

    LazyPlanNodePtr merged = clone_node_without_inputs(limit_node);
    merged->limit = std::min(limit_node->limit, child_node->limit);
    merged->inputs = {child_node->inputs[0]};
    return {merged, true, 1};
}

QueryOptimizer::ColumnRequirement QueryOptimizer::derive_child_requirement(
    const LazyPlanNodePtr& node,
    const ColumnRequirement& downstream_requirement,
    std::size_t child_index,
    OptimizationContext& context) const {
    (void)context;

    if (!node) {
        return {};
    }

    auto all_columns = []() {
        ColumnRequirement requirement;
        requirement.requires_all_columns = true;
        return requirement;
    };

    auto from_names = [](const std::vector<std::string>& names) {
        ColumnRequirement requirement;
        for (const auto& name : names) {
            requirement.names.insert(name);
        }
        return requirement;
    };

    auto from_expression = [&](const Expression& expression) {
        return from_names(expression.required_columns());
    };

    auto merge = [&](ColumnRequirement lhs, const ColumnRequirement& rhs) {
        if (lhs.requires_all_columns || rhs.requires_all_columns) {
            return all_columns();
        }
        lhs.names.insert(rhs.names.begin(), rhs.names.end());
        return lhs;
    };

    ColumnRequirement requirement;

    switch (node->kind) {
        case LazyNodeKind::ScanCsv:
        case LazyNodeKind::ScanParquet:
            requirement = downstream_requirement;
            break;

        case LazyNodeKind::SelectColumns:
            if (downstream_requirement.requires_all_columns) {
                requirement = from_names(node->columns);
            } else {
                for (const auto& name : node->columns) {
                    if (downstream_requirement.names.count(name) > 0) {
                        requirement.names.insert(name);
                    }
                }
            }
            break;

        case LazyNodeKind::SelectExpressions: {
            if (downstream_requirement.requires_all_columns) {
                requirement = all_columns();
                break;
            }

            for (std::size_t i = 0; i < node->expressions.size(); ++i) {
                const std::string name = default_expression_name(node->expressions[i], i);
                if (downstream_requirement.names.count(name) > 0) {
                    requirement = merge(
                        requirement,
                        from_expression(node->expressions[i]));
                }
            }
            break;
        }

        case LazyNodeKind::Filter:
            requirement = merge(
                downstream_requirement,
                node->predicate.has_value()
                    ? from_expression(*node->predicate)
                    : ColumnRequirement{}
            );
            break;

        case LazyNodeKind::WithColumn: {
            requirement = downstream_requirement;
            if (node->target_name.has_value()) {
                if (requirement.requires_all_columns ||
                    requirement.names.count(*node->target_name) > 0) {
                    if (!requirement.requires_all_columns) {
                        requirement.names.erase(*node->target_name);
                    }
                    if (!node->expressions.empty()) {
                        requirement = merge(
                            requirement,
                            from_expression(node->expressions[0]));
                    }
                }
            }
            break;
        }

        case LazyNodeKind::GroupBy:
            requirement = merge(downstream_requirement, from_names(node->columns));
            break;

        case LazyNodeKind::Aggregate: {
            if (child_index != 0 || !node->inputs[0] || node->inputs[0]->kind != LazyNodeKind::GroupBy) {
                requirement = all_columns();
                break;
            }

            requirement = from_names(node->inputs[0]->columns);
            for (const auto& [name, expr] : node->aggregations) {
                if (downstream_requirement.requires_all_columns ||
                    downstream_requirement.names.count(name) > 0) {
                    requirement = merge(requirement, from_expression(expr));
                }
            }
            break;
        }

        case LazyNodeKind::Join: {
            auto left_schema = output_schema(node->inputs[0], context);
            auto right_schema = output_schema(node->inputs[1], context);
            std::unordered_set<std::string> seen_names;

            std::unordered_set<std::string> left_outputs;
            for (const auto& field : left_schema->fields()) {
                if (!field) {
                    continue;
                }
                left_outputs.insert(field->name());
                seen_names.insert(field->name());
            }

            std::unordered_set<std::string> join_keys(node->columns.begin(), node->columns.end());
            std::unordered_set<std::string> right_outputs;
            for (const auto& field : right_schema->fields()) {
                if (!field || join_keys.count(field->name()) > 0) {
                    continue;
                }

                std::string output_name = field->name();
                if (seen_names.count(output_name) > 0) {
                    output_name += "_right";
                }
                output_name = unique_name(seen_names, output_name);
                right_outputs.insert(output_name);
            }

            if (downstream_requirement.requires_all_columns) {
                requirement = all_columns();
            } else {
                requirement = from_names(node->columns);
                if (child_index == 0) {
                    for (const auto& name : downstream_requirement.names) {
                        if (left_outputs.count(name) > 0) {
                            requirement.names.insert(name);
                        }
                    }
                } else {
                    std::map<std::string, std::string> output_to_right;
                    seen_names = {};
                    for (const auto& field : left_schema->fields()) {
                        if (field) {
                            seen_names.insert(field->name());
                        }
                    }
                    for (const auto& field : right_schema->fields()) {
                        if (!field || join_keys.count(field->name()) > 0) {
                            continue;
                        }
                        std::string output_name = field->name();
                        if (seen_names.count(output_name) > 0) {
                            output_name += "_right";
                        }
                        output_name = unique_name(seen_names, output_name);
                        output_to_right.emplace(output_name, field->name());
                    }
                    for (const auto& name : downstream_requirement.names) {
                        auto it = output_to_right.find(name);
                        if (it != output_to_right.end()) {
                            requirement.names.insert(it->second);
                        }
                    }
                }
            }
            break;
        }

        case LazyNodeKind::Sort:
            requirement = downstream_requirement;
            for (const auto& name : node->columns) {
                if (!requirement.requires_all_columns) {
                    requirement.names.insert(name);
                }
            }
            break;

        case LazyNodeKind::Head:
        case LazyNodeKind::SinkCsv:
        case LazyNodeKind::SinkParquet:
            requirement = downstream_requirement;
            break;

        case LazyNodeKind::InMemorySource:
            requirement = downstream_requirement;
            break;
    }

    return requirement;
}

QueryOptimizer::ColumnRequirement QueryOptimizer::root_requirement(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    auto schema = output_schema(root, context);
    ColumnRequirement requirement;
    for (const auto& name : schema_field_names(*schema)) {
        requirement.names.insert(name);
    }
    return requirement;
}

void QueryOptimizer::annotate_scan_projection(LazyPlanNode& scan_node,
                                              const ColumnRequirement& requirement,
                                              OptimizationContext& context) const {
    if (requirement.requires_all_columns) {
        scan_node.columns.clear();
        return;
    }

    auto full_scan = std::make_shared<LazyPlanNode>(scan_node);
    full_scan->columns.clear();
    auto full_schema = scan_schema(full_scan, context);
    std::vector<std::string> selected;
    selected.reserve(full_schema->num_fields());
    for (const auto& field : full_schema->fields()) {
        if (field && requirement.names.count(field->name()) > 0) {
            selected.push_back(field->name());
        }
    }
    scan_node.columns = std::move(selected);
}

std::shared_ptr<arrow::Schema> QueryOptimizer::output_schema(
    const LazyPlanNodePtr& root,
    OptimizationContext& context) const {
    if (!root) {
        optimizer_fail("cannot infer schema of a null plan");
    }

    auto cached = context.schema_cache.find(root->id);
    if (cached != context.schema_cache.end()) {
        return cached->second;
    }

    std::shared_ptr<arrow::Schema> schema;
    switch (root->kind) {
        case LazyNodeKind::ScanCsv:
        case LazyNodeKind::ScanParquet:
            schema = scan_schema(root, context);
            break;

        case LazyNodeKind::SelectColumns: {
            auto input_schema = output_schema(root->inputs.at(0), context);
            std::vector<std::shared_ptr<arrow::Field>> fields;
            fields.reserve(root->columns.size());
            for (const auto& name : root->columns) {
                auto field = input_schema->GetFieldByName(name);
                if (!field) {
                    throw ColumnNotFoundError("missing selected column: " + name);
                }
                fields.push_back(field);
            }
            schema = arrow::schema(fields);
            break;
        }

        case LazyNodeKind::SelectExpressions: {
            auto input_schema = output_schema(root->inputs.at(0), context);
            std::vector<std::shared_ptr<arrow::Field>> fields;
            fields.reserve(root->expressions.size());
            std::unordered_set<std::string> seen;
            for (std::size_t i = 0; i < root->expressions.size(); ++i) {
                const std::string name = default_expression_name(root->expressions[i], i);
                if (!seen.insert(name).second) {
                    throw SchemaError("duplicate output column name in select(expressions): " + name);
                }
                const DataType type = root->expressions[i].infer_type(*input_schema);
                auto arrow_type_result = to_arrow_type(type);
                if (!arrow_type_result.ok()) {
                    throw SchemaError(arrow_type_result.status().ToString());
                }
                fields.push_back(arrow::field(name, arrow_type_result.ValueOrDie()));
            }
            schema = arrow::schema(fields);
            break;
        }

        case LazyNodeKind::Filter:
        case LazyNodeKind::Sort:
        case LazyNodeKind::Head:
        case LazyNodeKind::SinkCsv:
        case LazyNodeKind::SinkParquet:
            schema = output_schema(root->inputs.at(0), context);
            break;

        case LazyNodeKind::WithColumn: {
            auto input_schema = output_schema(root->inputs.at(0), context);
            std::vector<std::shared_ptr<arrow::Field>> fields = input_schema->fields();
            const std::string target_name = root->target_name.value_or("");
            const DataType type = root->expressions.at(0).infer_type(*input_schema);
            auto arrow_type_result = to_arrow_type(type);
            if (!arrow_type_result.ok()) {
                throw SchemaError(arrow_type_result.status().ToString());
            }
            auto arrow_type = arrow_type_result.ValueOrDie();

            bool replaced = false;
            for (auto& field : fields) {
                if (field && field->name() == target_name) {
                    field = arrow::field(target_name, arrow_type);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                fields.push_back(arrow::field(target_name, arrow_type));
            }
            schema = arrow::schema(fields);
            break;
        }

        case LazyNodeKind::GroupBy: {
            auto input_schema = output_schema(root->inputs.at(0), context);
            std::vector<std::shared_ptr<arrow::Field>> fields;
            fields.reserve(root->columns.size());
            for (const auto& key : root->columns) {
                auto field = input_schema->GetFieldByName(key);
                if (!field) {
                    throw ColumnNotFoundError("missing group-by key: " + key);
                }
                fields.push_back(field);
            }
            schema = arrow::schema(fields);
            break;
        }

        case LazyNodeKind::Aggregate:
            schema = aggregate_output_schema(root, context);
            break;

        case LazyNodeKind::Join:
            schema = join_output_schema(root, context);
            break;

        case LazyNodeKind::InMemorySource:
            if (!root->in_memory_table) {
                optimizer_fail("in-memory source node is missing its table");
            }
            schema = root->in_memory_table->schema();
            break;
    }

    context.schema_cache.emplace(root->id, schema);
    return schema;
}

std::shared_ptr<arrow::Schema> QueryOptimizer::scan_schema(
    const LazyPlanNodePtr& scan_node,
    OptimizationContext& context) const {
    if (!scan_node || (scan_node->kind != LazyNodeKind::ScanCsv &&
                       scan_node->kind != LazyNodeKind::ScanParquet)) {
        optimizer_fail("scan_schema called on a non-scan node");
    }

    auto cached = context.scan_schema_cache.find(scan_node->path);
    std::shared_ptr<arrow::Schema> full_schema;
    if (cached != context.scan_schema_cache.end()) {
        full_schema = cached->second;
    } else {
        EagerDataFrame df = (scan_node->kind == LazyNodeKind::ScanCsv)
            ? EagerDataFrame::read_csv(scan_node->path)
            : EagerDataFrame::read_parquet(scan_node->path);
        full_schema = df.to_arrow_table()->schema();
        context.scan_schema_cache.emplace(scan_node->path, full_schema);
    }

    if (scan_node->columns.empty()) {
        return full_schema;
    }

    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(scan_node->columns.size());
    for (const auto& name : scan_node->columns) {
        auto field = full_schema->GetFieldByName(name);
        if (!field) {
            throw ColumnNotFoundError("missing projected scan column: " + name);
        }
        fields.push_back(field);
    }
    return arrow::schema(fields);
}

std::shared_ptr<arrow::Schema> QueryOptimizer::join_output_schema(
    const LazyPlanNodePtr& join_node,
    OptimizationContext& context) const {
    if (!join_node || join_node->inputs.size() != 2 || !join_node->inputs[0] || !join_node->inputs[1]) {
        optimizer_fail("join_output_schema received a malformed join node");
    }

    auto left_schema = output_schema(join_node->inputs[0], context);
    auto right_schema = output_schema(join_node->inputs[1], context);

    std::unordered_set<std::string> seen_names;
    std::unordered_set<std::string> join_keys(join_node->columns.begin(), join_node->columns.end());
    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(left_schema->num_fields() + right_schema->num_fields());

    for (const auto& field : left_schema->fields()) {
        if (!field) {
            continue;
        }
        seen_names.insert(field->name());
        fields.push_back(field);
    }

    for (const auto& field : right_schema->fields()) {
        if (!field || join_keys.count(field->name()) > 0) {
            continue;
        }

        std::string base = field->name();
        if (seen_names.count(base) > 0) {
            base += "_right";
        }
        const std::string unique = unique_name(seen_names, base);
        fields.push_back(arrow::field(unique, field->type()));
    }

    return arrow::schema(fields);
}

std::shared_ptr<arrow::Schema> QueryOptimizer::aggregate_output_schema(
    const LazyPlanNodePtr& aggregate_node,
    OptimizationContext& context) const {
    if (!aggregate_node || aggregate_node->inputs.size() != 1 || !aggregate_node->inputs[0] ||
        aggregate_node->inputs[0]->kind != LazyNodeKind::GroupBy) {
        optimizer_fail("aggregate_output_schema received a malformed aggregate node");
    }

    LazyPlanNodePtr group_node = aggregate_node->inputs[0];
    auto input_schema = output_schema(group_node->inputs.at(0), context);

    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(group_node->columns.size() + aggregate_node->aggregations.size());

    for (const auto& key : group_node->columns) {
        auto field = input_schema->GetFieldByName(key);
        if (!field) {
            throw ColumnNotFoundError("missing group-by key: " + key);
        }
        fields.push_back(field);
    }

    for (const auto& [name, expr] : aggregate_node->aggregations) {
        const DataType type = expr.infer_type(*input_schema);
        auto arrow_type_result = to_arrow_type(type);
        if (!arrow_type_result.ok()) {
            throw SchemaError(arrow_type_result.status().ToString());
        }
        fields.push_back(arrow::field(name, arrow_type_result.ValueOrDie()));
    }

    return arrow::schema(fields);
}

QueryOptimizer::ExpressionRewriteResult QueryOptimizer::rewrite_expression(
    const Expression& expression,
    const arrow::Schema* input_schema,
    bool fold_constants,
    bool simplify_expressions) const {
    (void)input_schema;

    bool changed = false;
    std::vector<Expression> rewritten_children;
    rewritten_children.reserve(expression.children().size());
    for (const auto& child : expression.children()) {
        if (!child) {
            throw TypeError("expression contains a null child");
        }
        ExpressionRewriteResult child_result =
            rewrite_expression(*child, input_schema, fold_constants, simplify_expressions);
        rewritten_children.push_back(child_result.expression.value_or(*child));
        changed = changed || child_result.changed;
    }

    Expression rebuilt = expression;
    if (expression.kind() == ExprKind::Alias) {
        if (rewritten_children.size() != 1) {
            throw TypeError("alias expression must have exactly one child");
        }
        rebuilt = preserve_alias(expression, rewritten_children[0]);
    } else if (!rewritten_children.empty()) {
        rebuilt = rebuild_expression(expression, rewritten_children);
    }

    if (fold_constants && rebuilt.is_constant()) {
        auto folded_value = evaluate_constant_expression(rebuilt);
        if (folded_value.has_value() && !is_null_literal_value(*folded_value)) {
            rebuilt = preserve_alias(expression, lit(*folded_value));
            changed = true;
        }
    }

    if (simplify_expressions) {
        ExpressionRewriteResult simplified = simplify_expression(rebuilt);
        if (simplified.expression.has_value()) {
            rebuilt = preserve_alias(expression, *simplified.expression);
            changed = changed || simplified.changed;
        }
    }

    return {rebuilt, changed};
}

std::optional<LiteralValue> QueryOptimizer::evaluate_constant_expression(
    const Expression& expression) const {
    if (!expression.is_constant()) {
        return std::nullopt;
    }

    switch (expression.kind()) {
        case ExprKind::ColumnRef:
        case ExprKind::AggregateOp:
            return std::nullopt;

        case ExprKind::Literal:
            return expression.literal();

        case ExprKind::Alias:
            if (expression.children().size() != 1 || !expression.children()[0]) {
                throw TypeError("alias expression must have exactly one child");
            }
            return evaluate_constant_expression(*expression.children()[0]);

        case ExprKind::IsNull: {
            if (expression.children().size() != 1 || !expression.children()[0]) {
                throw TypeError("is_null expression must have one child");
            }
            auto child = evaluate_constant_expression(*expression.children()[0]);
            if (!child.has_value()) {
                return std::nullopt;
            }
            return LiteralValue{is_null_literal_value(*child)};
        }

        case ExprKind::IsNotNull: {
            if (expression.children().size() != 1 || !expression.children()[0]) {
                throw TypeError("is_not_null expression must have one child");
            }
            auto child = evaluate_constant_expression(*expression.children()[0]);
            if (!child.has_value()) {
                return std::nullopt;
            }
            return LiteralValue{!is_null_literal_value(*child)};
        }

        case ExprKind::UnaryOp: {
            if (expression.children().size() != 1 || !expression.children()[0]) {
                throw TypeError("unary expression must have one child");
            }
            auto child = evaluate_constant_expression(*expression.children()[0]);
            if (!child.has_value()) {
                return std::nullopt;
            }
            if (is_null_literal_value(*child)) {
                return LiteralValue{NullType{}};
            }

            switch (expression.op()) {
                case OpKind::Abs: {
                    auto type = literal_type(*child);
                    if (!type.has_value() || !is_numeric(*type)) {
                        return std::nullopt;
                    }
                    return numeric_to_literal(std::abs(to_long_double(*child)), *type);
                }
                case OpKind::Not:
                    if (!std::holds_alternative<bool>(*child)) {
                        return std::nullopt;
                    }
                    return LiteralValue{!std::get<bool>(*child)};
                default:
                    return std::nullopt;
            }
        }

        case ExprKind::StringOp: {
            std::vector<LiteralValue> children;
            children.reserve(expression.children().size());
            for (const auto& child : expression.children()) {
                if (!child) {
                    throw TypeError("string expression contains a null child");
                }
                auto value = evaluate_constant_expression(*child);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                children.push_back(*value);
            }

            for (const auto& value : children) {
                if (is_null_literal_value(value)) {
                    return LiteralValue{NullType{}};
                }
            }

            switch (expression.op()) {
                case OpKind::Length:
                    if (children.size() != 1 || !std::holds_alternative<std::string>(children[0])) {
                        return std::nullopt;
                    }
                    return LiteralValue{static_cast<int32_t>(std::get<std::string>(children[0]).size())};

                case OpKind::ToLower: {
                    if (children.size() != 1 || !std::holds_alternative<std::string>(children[0])) {
                        return std::nullopt;
                    }
                    return LiteralValue{lowercase_ascii(std::get<std::string>(children[0]))};
                }

                case OpKind::ToUpper: {
                    if (children.size() != 1 || !std::holds_alternative<std::string>(children[0])) {
                        return std::nullopt;
                    }
                    std::string value = std::get<std::string>(children[0]);
                    std::transform(value.begin(), value.end(), value.begin(),
                                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
                    return LiteralValue{value};
                }

                case OpKind::Contains:
                    if (children.size() != 2 ||
                        !std::holds_alternative<std::string>(children[0]) ||
                        !std::holds_alternative<std::string>(children[1])) {
                        return std::nullopt;
                    }
                    return LiteralValue{
                        std::get<std::string>(children[0]).find(std::get<std::string>(children[1])) != std::string::npos
                    };

                case OpKind::StartsWith:
                    if (children.size() != 2 ||
                        !std::holds_alternative<std::string>(children[0]) ||
                        !std::holds_alternative<std::string>(children[1])) {
                        return std::nullopt;
                    }
                    return LiteralValue{
                        std::get<std::string>(children[0]).rfind(std::get<std::string>(children[1]), 0) == 0
                    };

                case OpKind::EndsWith: {
                    if (children.size() != 2 ||
                        !std::holds_alternative<std::string>(children[0]) ||
                        !std::holds_alternative<std::string>(children[1])) {
                        return std::nullopt;
                    }
                    const auto& text = std::get<std::string>(children[0]);
                    const auto& suffix = std::get<std::string>(children[1]);
                    const bool match = text.size() >= suffix.size() &&
                        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
                    return LiteralValue{match};
                }

                default:
                    return std::nullopt;
            }
        }

        case ExprKind::BinaryOp:
        case ExprKind::ComparisonOp:
        case ExprKind::BooleanOp: {
            if (expression.children().size() != 2 || !expression.children()[0] || !expression.children()[1]) {
                throw TypeError("binary expression must have two children");
            }

            auto lhs = evaluate_constant_expression(*expression.children()[0]);
            auto rhs = evaluate_constant_expression(*expression.children()[1]);
            if (!lhs.has_value() || !rhs.has_value()) {
                return std::nullopt;
            }

            if (is_null_literal_value(*lhs) || is_null_literal_value(*rhs)) {
                return LiteralValue{NullType{}};
            }

            if (expression.kind() == ExprKind::BinaryOp) {
                auto lhs_type = literal_type(*lhs);
                auto rhs_type = literal_type(*rhs);
                if (!lhs_type.has_value() || !rhs_type.has_value()) {
                    return std::nullopt;
                }

                switch (expression.op()) {
                    case OpKind::Add:
                    case OpKind::Subtract:
                    case OpKind::Multiply: {
                        if (!is_numeric(*lhs_type) || !is_numeric(*rhs_type)) {
                            return std::nullopt;
                        }
                        const DataType out_type = promote_numeric(*lhs_type, *rhs_type);
                        long double value = 0.0L;
                        if (expression.op() == OpKind::Add) {
                            value = to_long_double(*lhs) + to_long_double(*rhs);
                        } else if (expression.op() == OpKind::Subtract) {
                            value = to_long_double(*lhs) - to_long_double(*rhs);
                        } else {
                            value = to_long_double(*lhs) * to_long_double(*rhs);
                        }
                        return numeric_to_literal(value, out_type);
                    }

                    case OpKind::Divide:
                        if (!is_numeric(*lhs_type) || !is_numeric(*rhs_type)) {
                            return std::nullopt;
                        }
                        if (to_long_double(*rhs) == 0.0L) {
                            throw DataFrameError("division by zero");
                        }
                        return LiteralValue{
                            static_cast<double>(to_long_double(*lhs) / to_long_double(*rhs))
                        };

                    case OpKind::Modulo:
                        if (!is_integral(*lhs_type) || !is_integral(*rhs_type)) {
                            return std::nullopt;
                        }
                        if (to_long_double(*rhs) == 0.0L) {
                            throw DataFrameError("modulo by zero");
                        }
                        return numeric_to_literal(
                            static_cast<long long>(to_long_double(*lhs)) % static_cast<long long>(to_long_double(*rhs)),
                            promote_numeric(*lhs_type, *rhs_type)
                        );

                    default:
                        return std::nullopt;
                }
            }

            if (expression.kind() == ExprKind::ComparisonOp) {
                auto lhs_type = literal_type(*lhs);
                auto rhs_type = literal_type(*rhs);
                if (!lhs_type.has_value() || !rhs_type.has_value()) {
                    return std::nullopt;
                }

                if (is_numeric(*lhs_type) && is_numeric(*rhs_type)) {
                    const long double l = to_long_double(*lhs);
                    const long double r = to_long_double(*rhs);
                    switch (expression.op()) {
                        case OpKind::Equal:        return LiteralValue{l == r};
                        case OpKind::NotEqual:     return LiteralValue{l != r};
                        case OpKind::LessThan:     return LiteralValue{l < r};
                        case OpKind::LessEqual:    return LiteralValue{l <= r};
                        case OpKind::GreaterThan:  return LiteralValue{l > r};
                        case OpKind::GreaterEqual: return LiteralValue{l >= r};
                        default:                   return std::nullopt;
                    }
                }

                if (std::holds_alternative<std::string>(*lhs) && std::holds_alternative<std::string>(*rhs)) {
                    const auto& l = std::get<std::string>(*lhs);
                    const auto& r = std::get<std::string>(*rhs);
                    switch (expression.op()) {
                        case OpKind::Equal:        return LiteralValue{l == r};
                        case OpKind::NotEqual:     return LiteralValue{l != r};
                        case OpKind::LessThan:     return LiteralValue{l < r};
                        case OpKind::LessEqual:    return LiteralValue{l <= r};
                        case OpKind::GreaterThan:  return LiteralValue{l > r};
                        case OpKind::GreaterEqual: return LiteralValue{l >= r};
                        default:                   return std::nullopt;
                    }
                }

                if (std::holds_alternative<bool>(*lhs) && std::holds_alternative<bool>(*rhs)) {
                    const bool l = std::get<bool>(*lhs);
                    const bool r = std::get<bool>(*rhs);
                    switch (expression.op()) {
                        case OpKind::Equal:        return LiteralValue{l == r};
                        case OpKind::NotEqual:     return LiteralValue{l != r};
                        case OpKind::LessThan:     return LiteralValue{l < r};
                        case OpKind::LessEqual:    return LiteralValue{l <= r};
                        case OpKind::GreaterThan:  return LiteralValue{l > r};
                        case OpKind::GreaterEqual: return LiteralValue{l >= r};
                        default:                   return std::nullopt;
                    }
                }

                return std::nullopt;
            }

            if (!std::holds_alternative<bool>(*lhs) || !std::holds_alternative<bool>(*rhs)) {
                return std::nullopt;
            }

            switch (expression.op()) {
                case OpKind::And:
                    return LiteralValue{std::get<bool>(*lhs) && std::get<bool>(*rhs)};
                case OpKind::Or:
                    return LiteralValue{std::get<bool>(*lhs) || std::get<bool>(*rhs)};
                default:
                    return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

QueryOptimizer::ExpressionRewriteResult QueryOptimizer::simplify_expression(
    const Expression& expression) const {
    const auto& children = expression.children();

    switch (expression.kind()) {
        case ExprKind::BinaryOp:
            if (children.size() == 2 && children[0] && children[1]) {
                const Expression& lhs = *children[0];
                const Expression& rhs = *children[1];
                switch (expression.op()) {
                    case OpKind::Add:
                        if (is_zero_literal_expression(lhs)) return {rhs, true};
                        if (is_zero_literal_expression(rhs)) return {lhs, true};
                        break;
                    case OpKind::Subtract:
                        if (is_zero_literal_expression(rhs)) return {lhs, true};
                        break;
                    case OpKind::Multiply:
                        if (is_one_literal_expression(lhs)) return {rhs, true};
                        if (is_one_literal_expression(rhs)) return {lhs, true};
                        break;
                    case OpKind::Divide:
                        if (is_one_literal_expression(rhs)) return {lhs, true};
                        break;
                    default:
                        break;
                }
            }
            break;

        case ExprKind::BooleanOp:
            if (children.size() == 2 && children[0] && children[1]) {
                const Expression& lhs = *children[0];
                const Expression& rhs = *children[1];
                switch (expression.op()) {
                    case OpKind::And:
                        if (is_boolean_literal_expression(lhs, true)) return {rhs, true};
                        if (is_boolean_literal_expression(rhs, true)) return {lhs, true};
                        break;
                    case OpKind::Or:
                        if (is_boolean_literal_expression(lhs, false)) return {rhs, true};
                        if (is_boolean_literal_expression(rhs, false)) return {lhs, true};
                        break;
                    default:
                        break;
                }
            }
            break;

        case ExprKind::UnaryOp:
            if (expression.op() == OpKind::Not &&
                children.size() == 1 &&
                children[0] &&
                children[0]->kind() == ExprKind::UnaryOp &&
                children[0]->op() == OpKind::Not &&
                children[0]->children().size() == 1 &&
                children[0]->children()[0]) {
                return {*children[0]->children()[0], true};
            }
            break;

        default:
            break;
    }

    return {expression, false};
}

std::vector<Expression> QueryOptimizer::split_conjunctive_predicate(
    const Expression& predicate) const {
    std::vector<Expression> conjuncts;
    std::function<void(const Expression&)> split = [&](const Expression& expression) {
        const Expression& stripped = strip_alias(expression);
        if (stripped.kind() == ExprKind::BooleanOp &&
            stripped.op() == OpKind::And &&
            stripped.children().size() == 2 &&
            stripped.children()[0] &&
            stripped.children()[1]) {
            split(*stripped.children()[0]);
            split(*stripped.children()[1]);
            return;
        }
        conjuncts.push_back(expression);
    };

    split(predicate);
    return conjuncts;
}

Expression QueryOptimizer::combine_conjunctive_predicates(
    const std::vector<Expression>& conjuncts) const {
    if (conjuncts.empty()) {
        return lit(LiteralValue{true});
    }

    Expression combined = conjuncts.front();
    for (std::size_t i = 1; i < conjuncts.size(); ++i) {
        combined = make_and_expression(combined, conjuncts[i]);
    }
    return combined;
}

bool QueryOptimizer::expression_uses_only_columns(
    const Expression& expression,
    const std::unordered_set<std::string>& allowed_columns) const {
    for (const auto& name : expression.required_columns()) {
        if (allowed_columns.count(name) == 0) {
            return false;
        }
    }
    return true;
}

std::optional<Expression> QueryOptimizer::rewrite_expression_with_bindings(
    const Expression& expression,
    const std::map<std::string, Expression>& bindings) const {
    if (expression.kind() == ExprKind::ColumnRef && expression.name().has_value()) {
        auto it = bindings.find(*expression.name());
        if (it != bindings.end()) {
            return it->second;
        }
        return expression;
    }

    if (expression.kind() == ExprKind::Literal) {
        return expression;
    }

    std::vector<Expression> rewritten_children;
    rewritten_children.reserve(expression.children().size());
    for (const auto& child : expression.children()) {
        if (!child) {
            throw TypeError("expression contains a null child");
        }
        auto rewritten = rewrite_expression_with_bindings(*child, bindings);
        if (!rewritten.has_value()) {
            return std::nullopt;
        }
        rewritten_children.push_back(*rewritten);
    }

    if (expression.kind() == ExprKind::Alias) {
        if (rewritten_children.size() != 1) {
            return std::nullopt;
        }
        return preserve_alias(expression, rewritten_children[0]);
    }

    if (rewritten_children.empty()) {
        return expression;
    }

    return rebuild_expression(expression, rewritten_children);
}

std::map<std::string, Expression> QueryOptimizer::projection_bindings(
    const LazyPlanNodePtr& node) const {
    std::map<std::string, Expression> bindings;
    if (!node) {
        return bindings;
    }

    switch (node->kind) {
        case LazyNodeKind::SelectColumns:
            for (const auto& name : node->columns) {
                bindings.emplace(name, col(name));
            }
            break;

        case LazyNodeKind::SelectExpressions:
            for (std::size_t i = 0; i < node->expressions.size(); ++i) {
                bindings.emplace(default_expression_name(node->expressions[i], i), node->expressions[i]);
            }
            break;

        case LazyNodeKind::WithColumn:
            if (node->target_name.has_value() && node->expressions.size() == 1) {
                bindings.emplace(*node->target_name, node->expressions[0]);
            }
            break;

        default:
            break;
    }

    return bindings;
}

Expression QueryOptimizer::preserve_alias(const Expression& original,
                                          const Expression& rewritten) const {
    const std::string alias_name = original.alias_name();
    if (alias_name.empty() || rewritten.alias_name() == alias_name) {
        return rewritten;
    }
    return rewritten.with_alias(alias_name);
}
