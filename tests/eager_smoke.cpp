#include "DataFrameLib/ArrowUtils.h"
#include "DataFrameLib/Column.h"
#include "DataFrameLib/Eager.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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

} // namespace

int main() {
    const auto temp_root = std::filesystem::temp_directory_path() / "dataframelib_smoke";
    std::filesystem::create_directories(temp_root);

    const auto input_csv = temp_root / "input.csv";
    const auto projected_csv = temp_root / "projected.csv";
    const auto roundtrip_parquet = temp_root / "roundtrip.parquet";

    write_text_file(
        input_csv,
        "id,name,score,active\n"
        "1,Alice,9.5,true\n"
        "2,Bob,7.25,false\n"
        "3,Charlie,8.0,true\n"
    );

    auto df = EagerDataFrame::read_csv(input_csv.string());
    require(df.row_count() == 3, "read_csv should load 3 rows");
    require(df.column_count() == 4, "read_csv should load 4 columns");
    require(df.has_column("name"), "dataframe should contain 'name'");
    require(std::get<std::string>(df.column("name").value_at(1)) == "Bob",
            "second row should be Bob");

    auto selected = df.select(std::vector<std::string>{"name", "score"});
    require(selected.row_count() == 3, "select should preserve rows");
    require(selected.column_count() == 2, "select should keep requested columns");
    require(selected.schema().field(0)->name() == "name", "select should preserve requested order");

    auto first_two = df.head(2);
    require(first_two.row_count() == 2, "head(2) should return 2 rows");

    selected.write_csv(projected_csv.string());
    auto selected_roundtrip = EagerDataFrame::read_csv(projected_csv.string());
    require(selected_roundtrip.row_count() == 3, "CSV round-trip should preserve row count");
    require(selected_roundtrip.column_count() == 2, "CSV round-trip should preserve column count");

    df.write_parquet(roundtrip_parquet.string());
    auto parquet_roundtrip = EagerDataFrame::read_parquet(roundtrip_parquet.string());
    require(parquet_roundtrip.row_count() == 3, "Parquet round-trip should preserve row count");
    require(parquet_roundtrip.column_count() == 4, "Parquet round-trip should preserve column count");

    auto id_array_result = make_array_from_literals(
        {LiteralValue{int32_t{10}}, LiteralValue{int32_t{20}}, LiteralValue{int32_t{30}}},
        DataType::Int32
    );
    require(id_array_result.ok(), "failed to build int32 test array");

    auto label_array_result = make_array_from_literals(
        {LiteralValue{std::string("x")}, LiteralValue{std::string("y")}, LiteralValue{std::string("z")}},
        DataType::String
    );
    require(label_array_result.ok(), "failed to build string test array");

    EagerDataFrame constructed({
        {"id", Column("id", DataType::Int32, id_array_result.ValueOrDie())},
        {"label", Column("label", DataType::String, label_array_result.ValueOrDie())},
    });
    require(constructed.row_count() == 3, "column constructor should set row count");
    require(constructed.column_count() == 2, "column constructor should set column count");

    std::cout << "eager smoke tests passed" << std::endl;
    return 0;
}
