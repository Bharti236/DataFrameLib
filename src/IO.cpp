// ------------------------------------------------------------
//
// src/IO.cpp
// ------------------------------------------------------------
//
// This file should contain the actual Arrow reader/writer glue code.
//
// Responsibilities:
//
// 1) CSV reader implementation
//    - parse the file into Arrow arrays/tables,
//    - map Arrow types to the strict logical types,
//    - materialize an EagerDataFrame.
//
// 2) Parquet reader implementation
//    - open the file,
//    - load the table,
//    - materialize an EagerDataFrame.
//
// 3) CSV writer implementation
//    - convert the EagerDataFrame back to an Arrow table,
//    - write the table using Arrow CSV writers.
//
// 4) Parquet writer implementation
//    - convert the EagerDataFrame back to an Arrow table,
//    - write it using Arrow Parquet writers.
//
// Error handling expectations:
// - Any Arrow status failure should be converted into a project exception.
// - The message should tell the user which operation failed and why.
// - Avoid silent fallback behavior.