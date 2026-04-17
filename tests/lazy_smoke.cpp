#include "DataFrameLib/Eager.h"
#include "DataFrameLib/Lazy.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open " + path.string());
    }
    out << contents;
    if (!out) {
        throw std::runtime_error("failed to write " + path.string());
    }
}

bool is_null_value(const LiteralValue& value) {
    return std::holds_alternative<NullType>(value);
}

} // namespace

int main() {
    const auto temp_root = std::filesystem::temp_directory_path() / "dataframelib_lazy_smoke";
    std::filesystem::create_directories(temp_root);

    const auto employees_csv = temp_root / "employees.csv";
    const auto managers_csv = temp_root / "managers.csv";
    const auto explained_png = temp_root / "plan.png";
    const auto sink_csv_path = temp_root / "active.csv";
    const auto sink_parquet_path = temp_root / "top_scores.parquet";

    write_text_file(
        employees_csv,
        "id,name,dept,score,active\n"
        "1,Alice,eng,9.5,true\n"
        "2,Bob,sales,7.25,false\n"
        "3,Charlie,eng,8.0,true\n"
        "4,Dana,hr,6.5,true\n"
    );

    write_text_file(
        managers_csv,
        "id,manager\n"
        "1,Eve\n"
        "3,Frank\n"
        "4,Grace\n"
    );

    auto projected = scan_csv(employees_csv.string())
        .filter(col("score") > lit(LiteralValue{8.0}))
        .select(std::vector<Expression>{
            alias(to_upper(col("name")), "name_upper"),
            alias(col("score") / lit(LiteralValue{2.0}), "half_score"),
            alias(contains(col("name"), "a"), "has_a"),
        })
        .collect();

    require(projected.row_count() == 1, "filter + select(expressions) should keep one row");
    require(projected.column_count() == 3, "projection should create three columns");
    require(std::get<std::string>(projected.column("name_upper").value_at(0)) == "ALICE",
            "name_upper should be ALICE");
    require(std::get<double>(projected.column("half_score").value_at(0)) == 4.75,
            "half_score should be 4.75");
    require(!std::get<bool>(projected.column("has_a").value_at(0)),
            "contains() should be case-sensitive here");

    auto augmented = scan_csv(employees_csv.string())
        .with_column("score_plus_one", col("score") + lit(LiteralValue{1.0}))
        .collect();
    require(augmented.column_count() == 6, "with_column should append a new column");
    require(std::get<double>(augmented.column("score_plus_one").value_at(2)) == 9.0,
            "score_plus_one should evaluate correctly");

    auto sorted = scan_csv(employees_csv.string())
        .sort({"score"}, false)
        .head(2)
        .collect();
    require(sorted.row_count() == 2, "head after sort should keep two rows");
    require(std::get<std::string>(sorted.column("name").value_at(0)) == "Alice",
            "descending sort should place Alice first");
    require(std::get<std::string>(sorted.column("name").value_at(1)) == "Charlie",
            "descending sort should place Charlie second");

    auto grouped = scan_csv(employees_csv.string())
        .group_by({"dept"})
        .aggregate({
            {"avg_score", mean(col("score"))},
            {"count_id", count(col("id"))},
        })
        .sort({"dept"})
        .collect();

    require(grouped.row_count() == 3, "group_by should produce one row per department");
    require(std::get<std::string>(grouped.column("dept").value_at(0)) == "eng",
            "sorted grouped output should start with eng");
    require(std::get<double>(grouped.column("avg_score").value_at(0)) == 8.75,
            "eng average score should be 8.75");
    require(std::get<int64_t>(grouped.column("count_id").value_at(0)) == 2,
            "eng count should be 2");

    auto joined = scan_csv(employees_csv.string())
        .join(scan_csv(managers_csv.string()), {"id"}, JoinType::Left)
        .collect();

    require(joined.row_count() == 4, "left join should preserve all employee rows");
    require(std::get<std::string>(joined.column("manager").value_at(0)) == "Eve",
            "Alice should match manager Eve");
    require(is_null_value(joined.column("manager").value_at(1)),
            "Bob should have null manager after left join");

    auto lazy_plan = scan_csv(employees_csv.string())
        .filter(col("active") == lit(LiteralValue{true}))
        .head(2);

    lazy_plan.explain(explained_png.string());
    require(std::filesystem::exists(explained_png), "explain() should create the PNG file");
    require(std::filesystem::file_size(explained_png) > 0, "explain() PNG should not be empty");

    lazy_plan.sink_csv(sink_csv_path.string());
    auto active_rows = EagerDataFrame::read_csv(sink_csv_path.string());
    require(active_rows.row_count() == 2, "sink_csv should write the collected rows");

    scan_csv(employees_csv.string())
        .sort({"score"}, false)
        .head(3)
        .sink_parquet(sink_parquet_path.string());

    auto top_scores = scan_parquet(sink_parquet_path.string()).collect();
    require(top_scores.row_count() == 3, "sink_parquet + scan_parquet should round-trip three rows");
    require(std::get<std::string>(top_scores.column("name").value_at(0)) == "Alice",
            "parquet round-trip should preserve ordering");

    std::cout << "lazy smoke tests passed" << std::endl;
    return 0;
}
