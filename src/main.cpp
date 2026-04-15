#include "../include/DataFrameLib/Eager.h"
// #include "../include/DataFrameLib/Lazy.h"
#include <iostream>

using namespace DataFrameLib;

int main() {
    // --- Eager Mode Demo ---
    std::cout << "Running Eager Mode..." << std::endl;
    auto df = EagerDataFrame::read_csv("data.csv");
    
    // auto result = df.filter(col("age") > 30)
    // select
    // with_column
    // return new DataFrame
    // user expects original to remain intact

    return 0;
}