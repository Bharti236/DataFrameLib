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
    auto df = read_csv(input_dir + "/data.csv");

    // --- join_outer ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = left.join(right, {"id"}, "outer");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_outer:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_outer.csv");
        std::cout << "PASS:join_outer" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_outer:" << e.what() << std::endl;
    }

    // --- join_empty_inner: inner join on empty left side ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = left.filter(col("id") > 10000).join(right, {"id"}, "inner");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_empty_inner:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_empty_inner.csv");
        std::cout << "PASS:join_empty_inner" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_empty_inner:" << e.what() << std::endl;
    }

    // --- join_self: self-join with different column selections ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto df_left = df.select({"id", "name"});
        auto df_right = df.select({"id", "salary", "department"});
        auto result = df_left.join(df_right, {"id"}, "inner");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_self:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_self.csv");
        std::cout << "PASS:join_self" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_self:" << e.what() << std::endl;
    }

    // --- join_right_nulls: left join where right has fewer rows ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = left.join(right.filter(col("id") < 50), {"id"}, "left");
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:join_right_nulls:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/join_right_nulls.csv");
        std::cout << "PASS:join_right_nulls" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:join_right_nulls:" << e.what() << std::endl;
    }

    return 0;
}
