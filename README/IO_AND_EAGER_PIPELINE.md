# I/O And Eager Pipeline

This file groups the assignment requirements related to:

- Arrow-backed storage and file I/O
- eager materialized execution
- `from_columns(map)`

Relevant source files:

- `include/DataFrameLib/IO.h`
- `src/IO.cpp`
- `include/DataFrameLib/Eager.h`
- `src/Eager.cpp`

## 1. File I/O Pipeline

### `read_csv(path)`

Current pipeline:

1. Open the file with Arrow I/O.
2. Build an Arrow CSV `TableReader`.
3. Read the full table eagerly.
4. Validate that every column type is one of the supported strict types.
5. Wrap the table in `EagerDataFrame`.

This matches the assignment intent:

- Arrow is used for storage and I/O.
- The result is fully materialized.
- Unsupported schemas fail immediately.

### `read_parquet(path)`

Current pipeline:

1. Open the file with Arrow I/O.
2. Build a Parquet Arrow reader.
3. Read the full table eagerly.
4. Validate the schema against the strict supported type set.
5. Wrap the table in `EagerDataFrame`.

### `write_csv(path)` and `write_parquet(path)`

Current pipeline:

1. Convert `EagerDataFrame` to its Arrow table.
2. Validate the outgoing table.
3. Use Arrow CSV or Parquet writer APIs.
4. Close the file stream and propagate any failure.

## 2. DataFrame Construction From Columns

The strict requirements list `from_columns(map)` as a public construction path.

Current status:

- The code already had `EagerDataFrame(const std::map<std::string, Column>&)`.
- A free helper `from_columns(const std::map<std::string, Column>&)` has now been added so the API matches the assignment wording more closely.

Pipeline:

1. Accept a validated map of strict `Column` objects.
2. Check row-count consistency across all columns.
3. Build Arrow fields and chunked arrays.
4. Construct an `arrow::Table`.
5. Validate it and wrap it in `EagerDataFrame`.

## 3. What The Eager Layer Already Implements

Implemented today:

- Arrow-table construction and validation
- `read_csv`
- `read_parquet`
- `write_csv`
- `write_parquet`
- `row_count`
- `column_count`
- `schema`
- `has_column`
- `column`
- `select(vector<string>)`
- `head(n)`
- `group_by(keys)` helper construction
- `from_columns(map)` helper and constructor

Design properties already satisfied:

- eager operations return new frames instead of mutating in place,
- Arrow remains the storage backend,
- schema validation happens at frame boundaries,
- supported types are restricted to the assignment set.

## 4. What Is Still Missing In Eager Mode

The assignment requires both eager and lazy DataFrames to support the main dataframe operations. In the current code, these eager operations are still incomplete:

- `select(vector<Expression>)`
- `filter(predicate)`
- `with_column(name, expr)`
- `group_by(...).aggregate(...)`
- `join(other, on, how)`
- `sort(columns, ascending)`

There is now also a string-based `join(..., how)` overload for API compatibility, but it still routes into the same not-yet-implemented eager join path.

## 5. Why Lazy Queries Still Work Better Right Now

The lazy layer currently contains its own temporary executor in `src/Lazy.cpp`.

That executor already handles:

- expression evaluation,
- filtering,
- projection by expressions,
- grouped aggregation,
- joins,
- sorting,
- sinks,
- DAG rendering.

So the project is currently asymmetric:

- eager mode is complete for I/O and basic materialized inspection,
- lazy mode is much more operationally complete,
- but strict spec compliance still needs the eager execution gaps filled.

## 6. Next Work Needed For Full Compliance

To fully align this area with the assignment:

1. Move or share the expression-evaluation engine so eager and lazy use the same execution core.
2. Finish eager `filter`, `with_column`, `select(expressions)`, `join`, `sort`, and grouped `aggregate`.
3. Keep the free `from_columns(map)` helper in the public API.
4. Re-run smoke tests plus new eager operation tests once those implementations land.
