cmake_minimum_required(VERSION 3.10)
project(DataFrameLib)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

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
    src/Types.cpp
    src/Errors.cpp
    src/ArrowUtils.cpp
    src/Column.cpp
    src/Expression.cpp
    src/IO.cpp
    src/Eager.cpp 
    src/Lazy.cpp 
    src/Optimizer.cpp
)

target_include_directories(DataFrameEngine
    PUBLIC include
    PRIVATE include/DataFrameLib
)

# Link using the modern PkgConfig target
target_link_libraries(DataFrameEngine PUBLIC 
    Arrow::arrow_shared 
    Parquet::parquet_shared 
    PkgConfig::GVC 
)

# 4. Define your executable (The driver)
add_executable(df_app src/main.cpp)

# Link the executable to your library
target_link_libraries(df_app PRIVATE DataFrameEngine)

add_executable(df_smoke_tests tests/eager_smoke.cpp)
target_link_libraries(df_smoke_tests PRIVATE DataFrameEngine)

add_test(NAME eager_smoke COMMAND df_smoke_tests)
