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

    auto df = read_csv(input_dir + "/string_data.csv");

    // --- contains ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("email").contains("@gmail.com"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_contains:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/contains_result.csv");
        std::cout << "PASS:str_contains" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_contains:" << e.what() << std::endl;
    }

    // --- to_upper ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("name_upper", col("name").to_upper());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_upper:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/upper_result.csv");
        std::cout << "PASS:str_upper" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_upper:" << e.what() << std::endl;
    }

    // --- to_lower ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("name_lower", col("name").to_lower());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_lower:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lower_result.csv");
        std::cout << "PASS:str_lower" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_lower:" << e.what() << std::endl;
    }

    // --- starts_with ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("code").starts_with("A"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_startswith:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/startswith_result.csv");
        std::cout << "PASS:str_startswith" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_startswith:" << e.what() << std::endl;
    }

    // --- ends_with ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.filter(col("filename").ends_with(".txt"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_endswith:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/endswith_result.csv");
        std::cout << "PASS:str_endswith" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_endswith:" << e.what() << std::endl;
    }

    // --- length ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("name_len", col("name").length());
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:str_length:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/length_result.csv");
        std::cout << "PASS:str_length" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:str_length:" << e.what() << std::endl;
    }

    return 0;
}
