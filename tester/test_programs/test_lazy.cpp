#include <dataframelib/dataframelib.h>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <utility>

using namespace dataframelib;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_dir> <output_dir>" << std::endl;
        return 1;
    }
    std::string input_dir = argv[1];
    std::string output_dir = argv[2];

    // --- Lazy filter + collect ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto ldf = scan_csv(input_dir + "/data.csv");
        auto result = ldf.filter(col("age") > 30).collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_filter.csv");
        std::cout << "PASS:lazy_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_filter:" << e.what() << std::endl;
    }

    // --- Lazy select + collect ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto ldf = scan_csv(input_dir + "/data.csv");
        auto result = ldf.select({"name", "salary"}).collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_select:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_select.csv");
        std::cout << "PASS:lazy_select" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_select:" << e.what() << std::endl;
    }

    // --- Lazy group_by + aggregate + collect ---
    try {
        std::vector<std::pair<std::string, std::string>> aggs = {
            {"salary", "sum"},
            {"salary", "mean"},
            {"id", "count"},
        };

        auto t0 = std::chrono::high_resolution_clock::now();
        auto ldf = scan_csv(input_dir + "/data.csv");
        auto result = ldf.group_by({"department"}).aggregate(aggs).collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_groupby:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_groupby.csv");
        std::cout << "PASS:lazy_groupby" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_groupby:" << e.what() << std::endl;
    }

    // --- Lazy chained: filter + select + sort + collect ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto ldf = scan_csv(input_dir + "/data.csv");
        auto result = ldf.filter(col("salary") > 50000.0)
                         .select({"name", "salary", "department"})
                         .sort({"salary"}, false)
                         .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_chain:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_chain.csv");
        std::cout << "PASS:lazy_chain" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_chain:" << e.what() << std::endl;
    }

    // --- sink_csv ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto ldf = scan_csv(input_dir + "/data.csv");
        ldf.filter(col("age") > 30).sink_csv(output_dir + "/lazy_sink.csv");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_sink:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        std::cout << "PASS:lazy_sink" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_sink:" << e.what() << std::endl;
    }

    // --- explain (just check it doesn't crash) ---
    try {
        auto ldf = scan_csv(input_dir + "/data.csv");
        ldf.filter(col("age") > 30)
           .select({"name", "salary"})
           .explain(output_dir + "/lazy_explain.txt");
        std::cout << "PASS:lazy_explain" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_explain:" << e.what() << std::endl;
    }

    return 0;
}
