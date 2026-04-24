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

    // --- with_column: salary * 0.1 as bonus ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("bonus", col("salary") * 0.1);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:expr_bonus:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/expr_bonus.csv");
        std::cout << "PASS:expr_bonus" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:expr_bonus:" << e.what() << std::endl;
    }

    // --- with_column: abs(salary - 60000) ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("abs_val", (col("salary") - 60000.0).abs());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:expr_abs:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/expr_abs.csv");
        std::cout << "PASS:expr_abs" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:expr_abs:" << e.what() << std::endl;
    }

    // --- filter with complex predicate: (salary > 50000) & (age < 40) | (department == "HR") ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(
            ((col("salary") > 50000.0) & (col("age") < 40)) | (col("department") == "HR")
        );
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:expr_complex_filter:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/expr_complex_filter.csv");
        std::cout << "PASS:expr_complex_filter" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:expr_complex_filter:" << e.what() << std::endl;
    }

    // --- Arithmetic chaining: (salary + age * 100) / 2 ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("computed", (col("salary") + col("age") * 100) / 2.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:expr_arith_chain:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/expr_arith_chain.csv");
        std::cout << "PASS:expr_arith_chain" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:expr_arith_chain:" << e.what() << std::endl;
    }

    return 0;
}
