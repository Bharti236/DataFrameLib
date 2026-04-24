#include <dataframelib/dataframelib.h>

#include <arrow/array/builder_binary.h>
#include <arrow/array/builder_primitive.h>

#include <chrono>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace dataframelib;

static std::shared_ptr<arrow::Array> make_int64(const std::vector<int64_t>& vals) {
    arrow::Int64Builder b;
    ARROW_THROW_NOT_OK(b.AppendValues(vals));
    std::shared_ptr<arrow::Array> out;
    ARROW_THROW_NOT_OK(b.Finish(&out));
    return out;
}

static std::shared_ptr<arrow::Array> make_strings(const std::vector<std::string>& vals) {
    arrow::StringBuilder b;
    for (const auto& s : vals) {
        ARROW_THROW_NOT_OK(b.Append(s));
    }
    std::shared_ptr<arrow::Array> out;
    ARROW_THROW_NOT_OK(b.Finish(&out));
    return out;
}

static std::shared_ptr<arrow::Array> make_doubles(const std::vector<double>& vals) {
    arrow::DoubleBuilder b;
    ARROW_THROW_NOT_OK(b.AppendValues(vals));
    std::shared_ptr<arrow::Array> out;
    ARROW_THROW_NOT_OK(b.Finish(&out));
    return out;
}

static EagerDataFrame sample_from_columns() {
    std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>> cols;
    cols.emplace_back("id", make_int64({1, 2, 3, 4, 5}));
    cols.emplace_back("name", make_strings({"a", "b", "c", "d", "e"}));
    cols.emplace_back("score", make_doubles({10.5, 20.0, 30.25, 40.0, 50.5}));
    return from_columns(cols);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_dir> <output_dir>" << std::endl;
        return 1;
    }
    std::string output_dir = argv[2];

    // --- fc_simple: build from Arrow columns (map-like name -> array) ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto df = sample_from_columns();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:fc_simple:" << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << std::endl;
        df.write_csv(output_dir + "/fc_simple.csv");
        std::cout << "PASS:fc_simple" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:fc_simple:" << e.what() << std::endl;
    }

    // --- fc_filter: from_columns then filter ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = sample_from_columns().filter(col("score") > 25.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:fc_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << std::endl;
        result.write_csv(output_dir + "/fc_filter.csv");
        std::cout << "PASS:fc_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:fc_filter:" << e.what() << std::endl;
    }

    // --- fc_with_column: from_columns then derived column ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = sample_from_columns().with_column("total", col("score") * 2.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:fc_with_column:" << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << std::endl;
        result.write_csv(output_dir + "/fc_with_column.csv");
        std::cout << "PASS:fc_with_column" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:fc_with_column:" << e.what() << std::endl;
    }

    // --- fc_empty: zero-length columns ---
    try {
        arrow::Int64Builder ib;
        std::shared_ptr<arrow::Array> empty_ids;
        ARROW_THROW_NOT_OK(ib.Finish(&empty_ids));
        arrow::StringBuilder sb;
        std::shared_ptr<arrow::Array> empty_names;
        ARROW_THROW_NOT_OK(sb.Finish(&empty_names));

        auto t0 = std::chrono::high_resolution_clock::now();
        auto df = from_columns({{"id", empty_ids}, {"name", empty_names}});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:fc_empty:" << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << std::endl;
        df.write_csv(output_dir + "/fc_empty.csv");
        std::cout << "PASS:fc_empty" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:fc_empty:" << e.what() << std::endl;
    }

    // --- fc_select: subset of columns ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = sample_from_columns().select({"id", "score"});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:fc_select:" << std::chrono::duration<double, std::milli>(t1 - t0).count()
                  << std::endl;
        result.write_csv(output_dir + "/fc_select.csv");
        std::cout << "PASS:fc_select" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:fc_select:" << e.what() << std::endl;
    }

    return 0;
}
