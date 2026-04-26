# I/O And Eager Pipeline

Relevant files:

- `include/DataFrameLib/IO.h`
- `src/IO.cpp`
- `include/DataFrameLib/Eager.h`
- `src/Eager.cpp`

## File I/O

`read_csv(path)`:

1. Open the file through Arrow I/O.
2. Build an Arrow CSV table reader.
3. Materialize the table eagerly.
4. Validate the schema against the supported strict types.
5. Wrap the result in `EagerDataFrame`.

`read_parquet(path)`:

1. Open the file through Arrow I/O.
2. Build an Arrow Parquet reader.
3. Materialize the table eagerly.
4. Validate the schema.
5. Wrap the result in `EagerDataFrame`.

`write_csv(path)` and `write_parquet(path)`:

1. Convert the frame to its Arrow table.
2. Validate the outgoing schema.
3. Write with Arrow CSV or Parquet APIs.
4. Propagate any Arrow failure as a project exception.

## DataFrame Construction

Two public construction paths are supported:

- `from_columns(const std::map<std::string, Column>& columns)`
- `from_columns(const std::vector<std::pair<std::string, std::shared_ptr<arrow::Array>>>& columns)`

Both paths validate row-count consistency and supported Arrow types before returning an `EagerDataFrame`.

The Arrow-array overload exists specifically for instructor tester compatibility.

## Eager Execution

Implemented eager surface:

- `select(columns)`
- `select(expressions)`
- `filter(predicate)`
- `with_column(name, expr)`
- `group_by(keys).aggregate(...)`
- `join(other, on, how)`
- `sort(columns, ascending)`
- `head(n)`

Important implementation detail:

- simple structural operations such as `select(columns)` and `head(n)` operate directly on the materialized Arrow table
- expression-heavy eager operations reuse the same execution core as lazy mode by wrapping the current table as an in-memory lazy source and immediately calling `collect()`

That shared-path design is intentional. It keeps one source of truth for:

- expression evaluation
- grouped aggregation
- join semantics
- sorting behavior
- null handling

and avoids re-implementing the same logic twice in `Eager.cpp` and `Lazy.cpp`.

## Tester-Facing API Notes

The eager API now also includes:

- `num_rows()`
- `num_columns()`
- brace-list friendly `select({"a", "b"})`
- string join kinds such as `"inner"` and `"outer"`

These are there so the library compiles cleanly against `tester/test_programs`.
