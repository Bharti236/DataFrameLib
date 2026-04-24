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

    auto df = read_csv(input_dir + "/null_data.csv");

    // --- is_null ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("x").is_null());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:is_null:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/is_null_result.csv");
        std::cout << "PASS:is_null" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:is_null:" << e.what() << std::endl;
    }

    // --- is_not_null ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("x").is_not_null());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:is_not_null:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/is_not_null_result.csv");
        std::cout << "PASS:is_not_null" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:is_not_null:" << e.what() << std::endl;
    }

    // --- Arithmetic with nulls: x + y should be null where either is null ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("sum_xy", col("x") + col("y"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:null_arith:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/null_arith.csv");
        std::cout << "PASS:null_arith" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:null_arith:" << e.what() << std::endl;
    }

    // --- Filter not-null then compute ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("x").is_not_null() & col("y").is_not_null())
                        .with_column("product", col("x") * col("y"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:null_filter_compute:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/null_filter_compute.csv");
        std::cout << "PASS:null_filter_compute" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:null_filter_compute:" << e.what() << std::endl;
    }

    return 0;
}
