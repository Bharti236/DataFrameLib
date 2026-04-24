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

    // --- sort ascending ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.sort({"salary"}, true);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:sort_asc:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/sort_asc.csv");
        std::cout << "PASS:sort_asc" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:sort_asc:" << e.what() << std::endl;
    }

    // --- sort descending ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.sort({"salary"}, false);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:sort_desc:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/sort_desc.csv");
        std::cout << "PASS:sort_desc" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:sort_desc:" << e.what() << std::endl;
    }

    // --- head ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.head(5);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:head:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/head_result.csv");
        std::cout << "PASS:head" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:head:" << e.what() << std::endl;
    }

    // --- sort then head ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = df.sort({"salary"}, false).head(5);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:sort_head:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/sort_head.csv");
        std::cout << "PASS:sort_head" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:sort_head:" << e.what() << std::endl;
    }

    return 0;
}
