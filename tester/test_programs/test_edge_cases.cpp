#include <dataframelib/dataframelib.h>
#include <chrono>
#include <iostream>
#include <string>

using namespace dataframelib;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_dir> <output_dir>" << std::endl;
        return 1;
    }
    std::string input_dir = argv[1];
    std::string output_dir = argv[2];

    auto df = read_csv(input_dir + "/data.csv");

    // --- edge_empty_filter: filter that matches no rows ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") > 200);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_empty_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_empty_filter.csv");
        std::cout << "PASS:edge_empty_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_empty_filter:" << e.what() << std::endl;
    }

    // --- edge_head_zero: head(0) ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.head(0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_head_zero:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_head_zero.csv");
        std::cout << "PASS:edge_head_zero" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_head_zero:" << e.what() << std::endl;
    }

    // --- edge_head_large: head larger than row count ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.head(5000);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_head_large:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_head_large.csv");
        std::cout << "PASS:edge_head_large" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_head_large:" << e.what() << std::endl;
    }

    // --- edge_select_single: select a single column ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"name"});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_select_single:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_select_single.csv");
        std::cout << "PASS:edge_select_single" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_select_single:" << e.what() << std::endl;
    }

    // --- edge_replace_col: with_column replacing existing column ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("age", col("age") + 1);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_replace_col:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_replace_col.csv");
        std::cout << "PASS:edge_replace_col" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_replace_col:" << e.what() << std::endl;
    }

    // --- edge_filter_all: filter that matches all rows ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") > 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_filter_all:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_filter_all.csv");
        std::cout << "PASS:edge_filter_all" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_filter_all:" << e.what() << std::endl;
    }

    // --- edge_single_group: group_by on a filtered single-group result ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("department") == "HR")
                        .group_by({"department"})
                        .aggregate({{"salary", "sum"}, {"salary", "count"}});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_single_group:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_single_group.csv");
        std::cout << "PASS:edge_single_group" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_single_group:" << e.what() << std::endl;
    }

    // --- edge_double_sort: second sort overrides first ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.sort({"salary"}, true).sort({"age", "id"}, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:edge_double_sort:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/edge_double_sort.csv");
        std::cout << "PASS:edge_double_sort" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:edge_double_sort:" << e.what() << std::endl;
    }

    return 0;
}
