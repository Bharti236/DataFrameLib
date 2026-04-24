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

    // --- chain_filter_sort_head ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("salary") > 50000.0).sort({"age", "id"}, true).head(10);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:chain_filter_sort_head:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/chain_filter_sort_head.csv");
        std::cout << "PASS:chain_filter_sort_head" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:chain_filter_sort_head:" << e.what() << std::endl;
    }

    // --- chain_wc_filter_gb ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("bonus", col("salary") * 0.1)
                        .filter(col("bonus") > 10000.0)
                        .group_by({"department"})
                        .aggregate({{"salary", "sum"}, {"bonus", "mean"}});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:chain_wc_filter_gb:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/chain_wc_filter_gb.csv");
        std::cout << "PASS:chain_wc_filter_gb" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:chain_wc_filter_gb:" << e.what() << std::endl;
    }

    // --- chain_multi_filter ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") > 25)
                        .filter(col("salary") > 50000.0)
                        .filter(col("department") != "HR");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:chain_multi_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/chain_multi_filter.csv");
        std::cout << "PASS:chain_multi_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:chain_multi_filter:" << e.what() << std::endl;
    }

    // --- chain_select_wc ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"id", "name", "salary"})
                        .with_column("double_salary", col("salary") * 2);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:chain_select_wc:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/chain_select_wc.csv");
        std::cout << "PASS:chain_select_wc" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:chain_select_wc:" << e.what() << std::endl;
    }

    // --- chain_full_pipeline ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") >= 30)
                        .with_column("annual_bonus", col("salary") * 0.15)
                        .select({"name", "department", "salary", "annual_bonus"})
                        .sort({"salary", "name"}, false)
                        .head(20);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:chain_full_pipeline:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/chain_full_pipeline.csv");
        std::cout << "PASS:chain_full_pipeline" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:chain_full_pipeline:" << e.what() << std::endl;
    }

    return 0;
}
