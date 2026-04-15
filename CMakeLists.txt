cmake_minimum_required(VERSION 3.10)
project(DataFrameLib)

set(CMAKE_CXX_STANDARD 17)

# Enforce strict type safety and catch warnings as errors early
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# 1. Find Arrow and Parquet
find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)

# 2. Use PkgConfig to find Graphviz (libgvc and libcgraph)
find_package(PkgConfig REQUIRED)
# Added IMPORTED_TARGET to automatically handle includes and libraries
pkg_check_modules(GVC REQUIRED IMPORTED_TARGET libgvc libcgraph)

# 3. Define the Library (The core engine)
add_library(DataFrameEngine 
    src/Eager.cpp 
    src/Lazy.cpp 
    src/Optimizer.cpp
)

target_include_directories(DataFrameEngine PUBLIC include)

# Link using the modern PkgConfig target
target_link_libraries(DataFrameEngine PUBLIC 
    arrow_shared 
    parquet_shared 
    PkgConfig::GVC 
)

# 4. Define your executable (The driver)
add_executable(df_app src/main.cpp)

# Link the executable to your library
target_link_libraries(df_app PRIVATE DataFrameEngine)