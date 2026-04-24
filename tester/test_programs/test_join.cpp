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

    auto left = read_csv(input_dir + "/left.csv");
    auto right = read_csv(input_dir + "/right.csv");

    // --- Inner join ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = left.join(right, {"id"}, "inner");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_inner:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_inner.csv");
        std::cout << "PASS:join_inner" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_inner:" << e.what() << std::endl;
    }

    // --- Left join ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = left.join(right, {"id"}, "left");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_left:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_left.csv");
        std::cout << "PASS:join_left" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_left:" << e.what() << std::endl;
    }

    return 0;
}
