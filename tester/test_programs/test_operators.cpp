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

    // --- ops_subtract ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"id", "salary", "age"}).with_column("diff", col("salary") - col("age"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_subtract:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_subtract.csv");
        std::cout << "PASS:ops_subtract" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_subtract:" << e.what() << std::endl;
    }

    // --- ops_divide ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"id", "salary"}).with_column("monthly", col("salary") / 12.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_divide:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_divide.csv");
        std::cout << "PASS:ops_divide" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_divide:" << e.what() << std::endl;
    }

    // --- ops_modulo ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"id"}).with_column("id_mod", col("id") % 10);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_modulo:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_modulo.csv");
        std::cout << "PASS:ops_modulo" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_modulo:" << e.what() << std::endl;
    }

    // --- ops_less ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") < 25);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_less:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_less.csv");
        std::cout << "PASS:ops_less" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_less:" << e.what() << std::endl;
    }

    // --- ops_less_equal ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") <= 25);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_less_equal:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_less_equal.csv");
        std::cout << "PASS:ops_less_equal" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_less_equal:" << e.what() << std::endl;
    }

    // --- ops_greater_equal ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("age") >= 60);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_greater_equal:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_greater_equal.csv");
        std::cout << "PASS:ops_greater_equal" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_greater_equal:" << e.what() << std::endl;
    }

    // --- ops_not_equal ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("department") != "Engineering");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_not_equal:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_not_equal.csv");
        std::cout << "PASS:ops_not_equal" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_not_equal:" << e.what() << std::endl;
    }

    // --- ops_not ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(~(col("age") > 30));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_not:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_not.csv");
        std::cout << "PASS:ops_not" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_not:" << e.what() << std::endl;
    }

    // --- ops_nested_arith ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.select({"id", "salary", "age"})
                        .with_column("v", ((col("salary") - 50000.0) * 2.0 + col("age") * 100) / 3.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_nested_arith:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_nested_arith.csv");
        std::cout << "PASS:ops_nested_arith" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_nested_arith:" << e.what() << std::endl;
    }

    // --- ops_bool_combo ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(((col("age") >= 30) & (~(col("department") == "HR"))) | (col("salary") > 100000.0));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:ops_bool_combo:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/ops_bool_combo.csv");
        std::cout << "PASS:ops_bool_combo" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:ops_bool_combo:" << e.what() << std::endl;
    }

    return 0;
}
