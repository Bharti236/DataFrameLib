#include "../include/DataFrameLib/Eager.h"

#include <exception>
#include <iostream>
#include <string>

namespace {

bool has_suffix(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ./df_app <input.csv|input.parquet|input.pq>" << std::endl;
        return 1;
    }

    const std::string path = argv[1];

    try {
        EagerDataFrame df = has_suffix(path, ".csv")
            ? EagerDataFrame::read_csv(path)
            : EagerDataFrame::read_parquet(path);
        std::cout << df.to_string();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 2;
    }
}
