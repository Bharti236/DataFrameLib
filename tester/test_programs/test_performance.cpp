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

    std::string large_path = input_dir + "/large_data.csv";

    auto t0 = std::chrono::high_resolution_clock::now();
    auto df = read_csv(large_path);
    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "TIMING:perf_read_large:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;

    // --- perf_filter ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("value1") > 5000.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:perf_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/perf_filter.csv");
        std::cout << "PASS:perf_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:perf_filter:" << e.what() << std::endl;
    }

    // --- perf_groupby ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.group_by({"category"})
                        .aggregate({{"value1", "sum"}, {"value2", "mean"}, {"id", "count"}});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:perf_groupby:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/perf_groupby.csv");
        std::cout << "PASS:perf_groupby" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:perf_groupby:" << e.what() << std::endl;
    }

    // --- perf_sort ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.sort({"value1"}, true).head(100);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:perf_sort:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/perf_sort.csv");
        std::cout << "PASS:perf_sort" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:perf_sort:" << e.what() << std::endl;
    }

    // --- perf_lazy_chain ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = scan_csv(large_path)
                          .filter(col("value1") > 0.0)
                          .sort({"value2"}, false)
                          .head(50)
                          .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:perf_lazy_chain:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/perf_lazy_chain.csv");
        std::cout << "PASS:perf_lazy_chain" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:perf_lazy_chain:" << e.what() << std::endl;
    }

    return 0;
}
