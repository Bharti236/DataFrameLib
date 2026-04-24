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

    // --- lazy_join ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto ll = scan_csv(input_dir + "/left.csv");
        auto lr = scan_csv(input_dir + "/right.csv");
        auto result = ll.join(lr, {"id"}, "inner").collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_join:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_join.csv");
        std::cout << "PASS:lazy_join" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_join:" << e.what() << std::endl;
    }

    // --- lazy_with_column ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = scan_csv(input_dir + "/data.csv")
                          .with_column("bonus", col("salary") * 0.1)
                          .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_with_column:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_with_column.csv");
        std::cout << "PASS:lazy_with_column" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_with_column:" << e.what() << std::endl;
    }

    // --- lazy_sort_head ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = scan_csv(input_dir + "/data.csv")
                          .sort({"salary"}, false)
                          .head(10)
                          .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_sort_head:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_sort_head.csv");
        std::cout << "PASS:lazy_sort_head" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_sort_head:" << e.what() << std::endl;
    }

    // --- lazy_predicate_push ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = scan_csv(input_dir + "/data.csv")
                          .select({"name", "salary", "age"})
                          .filter(col("age") > 30)
                          .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_predicate_push:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_predicate_push.csv");
        std::cout << "PASS:lazy_predicate_push" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_predicate_push:" << e.what() << std::endl;
    }

    // --- lazy_full_pipeline ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = scan_csv(input_dir + "/data.csv")
                          .filter(col("age") >= 30)
                          .with_column("annual_bonus", col("salary") * 0.15)
                          .select({"name", "department", "salary", "annual_bonus"})
                          .sort({"salary", "name"}, false)
                          .head(20)
                          .collect();
        auto t1 = std::chrono::high_resolution_clock::now();
        std::cout << "TIMING:lazy_full_pipeline:" << std::chrono::duration<double, std::milli>(t1 - t0).count() << std::endl;
        result.write_csv(output_dir + "/lazy_full_pipeline.csv");
        std::cout << "PASS:lazy_full_pipeline" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:lazy_full_pipeline:" << e.what() << std::endl;
    }

    return 0;
}
