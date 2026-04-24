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

    // --- select ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"name", "salary"});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:select:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/select_result.csv");
        std::cout << "PASS:select" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:select:" << e.what() << std::endl;
    }

    // --- filter: age > 30 ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") > 30);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:filter_gt:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/filter_gt.csv");
        std::cout << "PASS:filter_gt" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:filter_gt:" << e.what() << std::endl;
    }

    // --- filter: department == "Engineering" ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("department") == "Engineering");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:filter_eq:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/filter_eq.csv");
        std::cout << "PASS:filter_eq" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:filter_eq:" << e.what() << std::endl;
    }

    // --- filter: compound (age > 25) & (salary > 50000) ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter((col("age") > 25) & (col("salary") > 50000.0));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:filter_compound:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/filter_compound.csv");
        std::cout << "PASS:filter_compound" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:filter_compound:" << e.what() << std::endl;
    }

    // --- filter then select ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") > 30).select({"name", "salary", "department"});
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:filter_select:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/filter_select.csv");
        std::cout << "PASS:filter_select" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:filter_select:" << e.what() << std::endl;
    }

    return 0;
}
