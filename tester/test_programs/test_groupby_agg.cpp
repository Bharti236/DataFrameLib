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

    auto df = read_csv(input_dir + "/data.csv");

    // --- Single-key group_by with multiple aggregations ---
    try {
        std::vector<std::pair<std::string, std::string>> aggs = {
            {"salary", "sum"},
            {"salary", "mean"},
            {"age", "min"},
            {"age", "max"},
            {"id", "count"},
        };

        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.group_by({"department"}).aggregate(aggs);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:groupby_single:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;

        result.write_csv(output_dir + "/groupby_result.csv");
        std::cout << "PASS:groupby_single" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:groupby_single:" << e.what() << std::endl;
    }

    // --- Multi-key group_by ---
    try {
        std::vector<std::pair<std::string, std::string>> aggs = {
            {"salary", "mean"},
            {"id", "count"},
        };

        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.group_by({"department", "city"}).aggregate(aggs);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:groupby_multi:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;

        result.write_csv(output_dir + "/groupby_multi.csv");
        std::cout << "PASS:groupby_multi" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:groupby_multi:" << e.what() << std::endl;
    }

    return 0;
}
