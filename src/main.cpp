#include "../include/DataFrameLib/Eager.h"
#include "../include/DataFrameLib/Lazy.h"

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
    try {
        if (argc == 2) {
            const std::string path = argv[1];
            EagerDataFrame df = has_suffix(path, ".csv")
                ? EagerDataFrame::read_csv(path)
                : EagerDataFrame::read_parquet(path);
            std::cout << df.to_string();
            return 0;
        }

        if (argc == 3 && std::string(argv[1]) == "lazy") {
            const std::string path = argv[2];
            LazyDataFrame df = has_suffix(path, ".csv")
                ? scan_csv(path)
                : scan_parquet(path);
            std::cout << df.collect().to_string();
            return 0;
        }

        if (argc == 4 && std::string(argv[1]) == "lazy") {
            const std::string path = argv[2];
            const std::string explain_path = argv[3];
            LazyDataFrame df = has_suffix(path, ".csv")
                ? scan_csv(path)
                : scan_parquet(path);
            df.explain(explain_path);
            std::cout << df.collect().to_string();
            return 0;
        }

        std::cerr
            << "Usage:\n"
            << "  ./df_app <input.csv|input.parquet|input.pq>\n"
            << "  ./df_app lazy <input.csv|input.parquet|input.pq>\n"
            << "  ./df_app lazy <input.csv|input.parquet|input.pq> <plan.png>\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return 2;
    }
}
