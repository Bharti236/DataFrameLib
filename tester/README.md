# COP290 Assignment 4 — DataFrameLib Autograder

Please note that you would need to wrap your library in a namespace called `dataframelib` for the test programs to work.


## Installation

```bash
cd tester
pip install -r requirements.txt
```

## Usage

```bash
python autograder.py --student-dir /path/to/student/project
```

### Options

| Flag | Description |
|---|---|
| `--student-dir PATH` | **(required)** Path to the student's project root. Must contain a `CMakeLists.txt` that defines a `dataframelib` target. |
| `--output-dir DIR` | Output directory for build artifacts, test data, and reports. Default: `results/` |
| `--data-dir DIR` | Use pre-generated test data instead of generating fresh data. |
| `--skip-build` | Skip the CMake configure/build step (re-use a previous build). |
| `--tests T [T ...]` | Run only specific test programs, e.g. `--tests test_io test_join`. |

### Examples

```bash
# Full run
python autograder.py --student-dir ~/submissions/student42/

# Re-run only join and lazy tests without rebuilding
python autograder.py --student-dir ~/submissions/student42/ --skip-build --tests test_join test_lazy

# Use pre-generated data
python generate_data.py --output-dir my_data/
python autograder.py --student-dir ~/submissions/student42/ --data-dir my_data/
```

## How It Works

1. **Data Generation** — Creates reproducible CSV and Parquet test files with `random.seed(42)`:
   - `data.csv` (1000 rows) — main dataset with id, name, age, salary, department, city
   - `left.csv` / `right.csv` (500 rows each, ~60% ID overlap) — for join tests
   - `string_data.csv` (200 rows) — for string operation tests
   - `null_data.csv` (100 rows, ~15% nulls) — for null handling tests
   - `large_data.csv` (100,000 rows) — for performance benchmarking
   - `data.parquet` — Parquet version of data.csv

2. **Build** — Runs CMake to build the student library and all test programs, linking each test executable against the `dataframelib` target.

3. **Execution** — Each test program receives `<input_dir>` and `<output_dir>` as command-line arguments, performs operations via the student API, and writes result CSVs.

4. **Validation** — Reads each output CSV and compares against Pandas-computed expected results:
   - Shape (rows and columns) must match
   - Column names must match (order-insensitive)
   - Float values compared with `numpy.allclose(rtol=1e-5)`
   - String values compared exactly
   - Null positions must match
   - Row order is ignored except for sort/head tests

5. **Reporting** — Prints a summary table and writes `report.json`.

## Troubleshooting

### Build fails: "dataframelib target not found"
The student's `CMakeLists.txt` must define a library target named `dataframelib`. Verify with:
```bash
grep -r "add_library.*dataframelib" /path/to/student/project/
```

### Build fails: Arrow not found
Ensure Apache Arrow C++ is installed and discoverable:
```bash
# Ubuntu/Debian
sudo apt install libarrow-dev libparquet-dev

# macOS (Homebrew)
brew install apache-arrow

# Verify
pkg-config --cflags --libs arrow
```

DISCUSS OTHER DETAILS ON PIAZZA!!

### Test times out (120s default)
Some operations on `large_data.csv` may be slow for unoptimized implementations. The timeout applies per test program, not per sub-test. Consider running individual tests with `--tests`.

### Float comparison failures
The autograder uses `rtol=1e-5` for floating-point comparisons. If the student uses a different precision path through Arrow, minor discrepancies are possible. Check the diff magnitude in the error message.

### CSV column ordering
The autograder compares column sets, not column order. However, column names must match exactly (e.g., group-by aggregation results should be named `salary_sum`, `salary_mean`, etc.).
