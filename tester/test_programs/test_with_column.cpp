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

    // --- salary doubled ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("salary_doubled", col("salary") * 2);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:wc_doubled:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/wc_doubled.csv");
        std::cout << "PASS:wc_doubled" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:wc_doubled:" << e.what() << std::endl;
    }

    // --- salary + age (type promotion: int + float) ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.with_column("total", col("salary") + col("age"));
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:wc_sum:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/wc_sum.csv");
        std::cout << "PASS:wc_sum" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:wc_sum:" << e.what() << std::endl;
    }

    return 0;
}
