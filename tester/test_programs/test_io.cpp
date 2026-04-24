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

    // --- Test 1: CSV round-trip ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto df = read_csv(input_dir + "/data.csv");
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms_read = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "TIMING:read_csv:" << ms_read << std::endl;

        t0 = std::chrono::high_resolution_clock::now();
        df.write_csv(output_dir + "/io_csv_roundtrip.csv");
        t1 = std::chrono::high_resolution_clock::now();
        double ms_write = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "TIMING:write_csv:" << ms_write << std::endl;

        auto df2 = read_csv(output_dir + "/io_csv_roundtrip.csv");
        if (df.num_rows() == df2.num_rows() && df.num_columns() == df2.num_columns()) {
            std::cout << "PASS:csv_roundtrip" << std::endl;
        } else {
            std::cout << "ERROR:csv_roundtrip:row/col count mismatch ("
                      << df.num_rows() << "x" << df.num_columns() << " vs "
                      << df2.num_rows() << "x" << df2.num_columns() << ")" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR:csv_roundtrip:" << e.what() << std::endl;
    }

    // --- Test 2: Parquet round-trip ---
    try {
        auto df = read_csv(input_dir + "/data.csv");

        auto t0 = std::chrono::high_resolution_clock::now();
        df.write_parquet(output_dir + "/io_parquet_roundtrip.parquet");
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms_wp = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "TIMING:write_parquet:" << ms_wp << std::endl;

        t0 = std::chrono::high_resolution_clock::now();
        auto df3 = read_parquet(output_dir + "/io_parquet_roundtrip.parquet");
        t1 = std::chrono::high_resolution_clock::now();
        double ms_rp = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "TIMING:read_parquet:" << ms_rp << std::endl;

        df3.write_csv(output_dir + "/io_parquet_as_csv.csv");

        if (df.num_rows() == df3.num_rows() && df.num_columns() == df3.num_columns()) {
            std::cout << "PASS:parquet_roundtrip" << std::endl;
        } else {
            std::cout << "ERROR:parquet_roundtrip:row/col count mismatch" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR:parquet_roundtrip:" << e.what() << std::endl;
    }

    // --- Test 3: Read the parquet file written by test 2 ---
    try {
        auto t0 = std::chrono::high_resolution_clock::now();
        auto df = read_parquet(output_dir + "/io_parquet_roundtrip.parquet");
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::cout << "TIMING:read_parquet_direct:" << ms << std::endl;

        df.write_csv(output_dir + "/io_from_parquet.csv");
        std::cout << "PASS:read_parquet_direct" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR:read_parquet_direct:" << e.what() << std::endl;
    }

    return 0;
}
