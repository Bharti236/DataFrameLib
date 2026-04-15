Step 2: Create a Modular Directory Structure
Organize your project to separate the eager logic, the lazy evaluation graph, and the expression system.

DataFrameLib/\
├── CMakeLists.txt\
├── include/\
│   ├── DataFrameLib/\
│   │   ├── Types.h        # Strict type definitions (int32, string, etc.)\
│   │   ├── Eager.h        # EagerDataFrame class\
│   │   ├── Lazy.h         # LazyDataFrame & DAG classes\
│   │   └── Optimizer.h    # QueryOptimizer rules\
├── src/\
│   ├── Eager.cpp\
│   ├── Lazy.cpp\
│   └── Optimizer.cpp\
└── tests/

Step 3: Configure strict CMake constraints
To enforce the strict type safety required for this assignment, configuring aggressive compiler flags in your standard CMakeLists.txt is highly recommended. You must also link Arrow and Graphviz.

# Go to build directory
```bash
mkdir -p build && cd build
```

# Configure and Compile
```bash
cmake ..
make
```

# Run your program
```bash
./df_app
```

### **Phase 3: Implementation Outline**

I highly recommend building this incrementally. Do not try to write the optimizer before the eager DataFrame works!

#### **Step 1: Core Data Structures & Type Safety**
* Create your base column structures using Arrow.
* [cite_start]Enforce strict immutable types: `int32`, `int64`, `float32`, `float64`, `string`, and `boolean`[cite: 33].
* [cite_start]Implement null handling (using strict `null` representation, not `NaN`)[cite: 34]. [cite_start]Ensure operations involving a null operand produce a null result[cite: 37].
* [cite_start]Implement type promotion rules (e.g., `int` + `float` $\rightarrow$ `float`) and throw immediate errors for incompatible operations[cite: 35, 36].

Step 1 is really about building a strong type system, null semantics, and Arrow column wrappers. The eager/lazy APIs should sit on top of that, not mix into it.

DataFrameLib/\
├── CMakeLists.txt\
├── include/\
│   └── DataFrameLib/\
│       ├── Types.h\
│       ├── Errors.h\
│       ├── ArrowUtils.h\
│       ├── Column.h\
│       ├── Expression.h\
│       ├── IO.h\
│       ├── Eager.h\
│       ├── Lazy.h\
│       └── Optimizer.h\
├── src/\
│   ├── Types.cpp\
│   ├── Errors.cpp\
│   ├── ArrowUtils.cpp\
│   ├── Column.cpp\
│   ├── Expression.cpp\
│   ├── IO.cpp\
│   ├── Eager.cpp\
│   ├── Lazy.cpp\
│   └── Optimizer.cpp\
└── tests/

```cpp
// ===============================
// Step 1 implementation order
// ===============================
//
// 1) Types.h / Types.cpp
//    Define the supported logical types, promotion rules, and compatibility checks.
//
// 2) Errors.h / Errors.cpp
//    Define a clear exception hierarchy and uniform error helpers.
//
// 3) ArrowUtils.h / ArrowUtils.cpp
//    Connect the logical type system to Arrow scalars, arrays, and casts.
//
// 4) Column.h / Column.cpp
//    Build the immutable Arrow-backed column abstraction with null propagation.
//
// 5) Expression.h / Expression.cpp
//    Build the typed symbolic expression layer used by later DataFrame APIs.
```
```cpp
// ============================================================
// Step 2 implementation order
// ============================================================
//
// 1) IO.h / IO.cpp
//    Get CSV and Parquet loading/writing working with strict types.
//
// 2) Eager.h / Eager.cpp
//    Implement construction, schema validation, select, filter, with_column,
//    and head first.
//
// 3) Grouping support
//    Add group_by and aggregate after basic row-wise operations are stable.
//
// 4) Join and sort
//    Add the more complex row-reordering and row-combining operations last.
//
// 5) Add tests
//    Test null propagation, promotion, schema errors, and I/O round-trips.
```

#### **Step 2: I/O Functions & Eager DataFrame**
* [cite_start]Implement the `EagerDataFrame` class[cite: 19].
* [cite_start]Build the I/O functions to load CSV and Parquet files into memory (`read_csv`, `read_parquet`) and write them back out (`write_csv`, `write_parquet`)[cite: 43].
* [cite_start]Implement the core operations eagerly: `select`, `filter`, `with_column`, `group_by`, `aggregate`, `join`, `sort`, and `head`[cite: 46].

#### **Step 3: The Expression System**
* Build an Abstract Syntax Tree (AST) structure to represent operations without executing them immediately.
* [cite_start]Implement Core Expressions: `col`, `lit`, and `alias`[cite: 49].
* [cite_start]Implement Arithmetic (`+`, `-`, `*`, `/`, `%`, `abs`), Comparison/Boolean (`==`, `>`, `&`, `is_null`, etc.), String (`length`, `starts_with`, `to_lower`, etc.), and Aggregation (`sum`, `mean`, `count`, etc.) expressions[cite: 51, 53, 57, 60].

#### **Step 4: Lazy DataFrame & DAG Construction**
* [cite_start]Implement the `LazyDataFrame` class[cite: 20].
* [cite_start]Instead of processing data, make your operations (`scan_csv`, `filter`, `join`, etc.) append nodes to a computation DAG[cite: 62, 63, 64].
* [cite_start]Implement the `explain(path)` method using Graphviz/Boost.Graph to dump a visual representation (`.png`) of your DAG[cite: 70].
* [cite_start]Implement the `collect()` method, which triggers actual materialization of the graph into results[cite: 69].

#### **Step 5: The Query Optimizer (Crucial for Performance Grades)**
* [cite_start]Build the `QueryOptimizer` to transform the unoptimized DAG before `collect()` executes it[cite: 21, 24].
* [cite_start]Implement baseline rules: Predicate Pushdown, Projection Pushdown, Constant Folding, Expression Simplification (e.g., transforming `x * 1 + 0` to `x`), and Limit Pushdown[cite: 78].
* [cite_start]*Note:* Performance is graded relative to other submissions and an unoptimized baseline[cite: 76].

---

### **Phase 4: Finalizing & Submission**

* [cite_start]**Design Document (`report.pdf`):** You must document your architecture and prove the correctness of each optimization you implemented, including concrete examples and expected performance benefits[cite: 79, 123].
* [cite_start]**Code Quality:** Ensure your public API exactly matches the provided document signatures[cite: 115]. [cite_start]Ensure proper memory management (smart pointers, RAII, move semantics) to avoid memory leaks (worth 15% of the grade)[cite: 110].
* [cite_start]**Packaging:** Place all files in a `project` directory[cite: 131]. [cite_start]Run `tar -cvf <entry_number>.tar project` (e.g., `2025ANZ8223.tar`) from the parent directory[cite: 132, 133, 134]. [cite_start]Submit via Moodle[cite: 136].

---

### **Pro-Tips for Success**
1.  [cite_start]**Strict API Compliance:** The grading is largely automated (45% for correctness)[cite: 110]. [cite_start]If your function names or signatures deviate from the assignment document, tests will fail[cite: 115].
2.  [cite_start]**Memory is Money:** You get graded on memory leaks and proper use of C++ semantics[cite: 110]. Use tools like Valgrind or AddressSanitizer during development.
3.  [cite_start]**Prepare for the Viva:** 15% of your grade is a live viva/demo[cite: 110]. [cite_start]Even if your code works perfectly, a poor viva can drastically reduce your marks[cite: 116, 129]. Make sure you deeply understand *why* your query optimizer works, not just *how* you coded it.
