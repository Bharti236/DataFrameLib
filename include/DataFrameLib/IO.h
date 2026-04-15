// ============================================================
// include/DataFrameLib/IO.h
// ============================================================
//
// Purpose:
// This header exposes the public file I/O entry points for the eager
// execution path only.
//
// In Step 2, these functions are responsible for turning external CSV or
// Parquet files into fully materialized EagerDataFrame objects, and for
// writing an already-materialized EagerDataFrame back to disk.
//
// IMPORTANT:
// - These functions are eager, so they must load or serialize immediately.
// - They must use Apache Arrow for all file I/O.
// - They must not create lazy plans.
// - They must not defer any work.
//
// Public API expected here:
//
// EagerDataFrame read_csv(const std::string& path);
//   Reads a CSV file from disk.
//   The result must be a fully materialized in-memory EagerDataFrame.
//   This function should:
//   - open the CSV source using Arrow CSV readers,
//   - infer or validate schema according to your strict types,
//   - convert supported Arrow types into the library's strict types,
//   - preserve nulls as Arrow nulls,
//   - throw a clear error if the file cannot be parsed or contains
//     unsupported types.
//
// EagerDataFrame read_parquet(const std::string& path);
//   Reads a Parquet file from disk.
//   The result must be a fully materialized in-memory EagerDataFrame.
//   This function should:
//   - use Arrow Parquet readers,
//   - load all required columns into memory,
//   - validate that every column matches one of the allowed logical types,
//   - preserve nulls exactly,
//   - reject unsupported schemas immediately.
//
// void write_csv(const EagerDataFrame& df, const std::string& path);
//   Serializes the complete eager DataFrame to a CSV file.
//   This should write the current materialized state of the frame, not a plan.
//   Nulls should be emitted using Arrow's standard CSV null representation.
//   The function must not mutate df.
//
// void write_parquet(const EagerDataFrame& df, const std::string& path);
//   Serializes the complete eager DataFrame to a Parquet file.
//   This should preserve schema and nulls through Arrow.
//
// Optional but useful internal helpers in this header or its cpp file:
//
// std::shared_ptr<arrow::Table> to_arrow_table(const EagerDataFrame& df);
//   Converts the eager frame into a single Arrow table for writing.
//
// EagerDataFrame from_arrow_table(std::shared_ptr<arrow::Table> table);
//   Builds an eager frame from a validated Arrow table.
//   Useful for read_csv, read_parquet, and internal transforms.
//
// Detailed behavioral rules:
//
// - If a file has unsupported column types, throw immediately.
// - If a CSV file has inconsistent row widths or malformed data, throw.
// - If a Parquet file contains nested or unsupported Arrow types, throw.
// - If all columns are valid, the loaded result must be ready for immediate
//   use by select/filter/join/group_by/etc.
// - Reading should preserve column names from the source file.