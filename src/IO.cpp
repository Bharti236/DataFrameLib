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
#include "../include/DataFrameLib/IO.h"
#include "../include/DataFrameLib/Eager.h"
#include "../include/DataFrameLib/Errors.h"

#include <arrow/api.h>
#include <arrow/csv/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void io_fail(const std::string& op, const std::string& why) {
    throw DataFrameError(op + ": " + why);
}

void check_arrow_status(const arrow::Status& st, const std::string& op) {
    if (!st.ok()) {
        io_fail(op, st.ToString());
    }
}

DataType arrow_type_to_datatype_impl(const std::shared_ptr<arrow::DataType>& type) {
    if (!type) {
        throw TypeError("null Arrow type");
    }

    switch (type->id()) {
        case arrow::Type::INT32:        return DataType::Int32;
        case arrow::Type::INT64:        return DataType::Int64;
        case arrow::Type::FLOAT:        return DataType::Float32;
        case arrow::Type::DOUBLE:       return DataType::Float64;
        case arrow::Type::UTF8:
        case arrow::Type::LARGE_STRING: return DataType::String;
        case arrow::Type::BOOL:         return DataType::Boolean;
        default:
            throw TypeError("unsupported Arrow type: " + type->ToString());
    }
}

void validate_schema_strict(const std::shared_ptr<arrow::Schema>& schema) {
    if (!schema) {
        throw SchemaError("Arrow schema is null");
    }

    std::unordered_set<std::string> seen;
    for (const auto& field : schema->fields()) {
        if (!field) {
            throw SchemaError("schema contains a null field");
        }
        if (!seen.insert(field->name()).second) {
            throw SchemaError("duplicate column name: " + field->name());
        }
        (void)arrow_type_to_datatype_impl(field->type());
    }
}

void validate_table_strict(const std::shared_ptr<arrow::Table>& table) {
    if (!table) {
        throw SchemaError("Arrow table is null");
    }
    validate_schema_strict(table->schema());
}

} // namespace

std::shared_ptr<arrow::Table> to_arrow_table(const EagerDataFrame& df) {
    auto table = df.to_arrow_table();
    validate_table_strict(table);
    return table;
}

EagerDataFrame from_arrow_table(std::shared_ptr<arrow::Table> table) {
    validate_table_strict(table);
    return EagerDataFrame(std::move(table));
}

EagerDataFrame read_csv(const std::string& path) {
    auto input_res = arrow::io::ReadableFile::Open(path);
    if (!input_res.ok()) {
        io_fail("read_csv", "failed to open '" + path + "': " + input_res.status().ToString());
    }
    std::shared_ptr<arrow::io::ReadableFile> input = *std::move(input_res);

    auto read_options = arrow::csv::ReadOptions::Defaults();
    auto parse_options = arrow::csv::ParseOptions::Defaults();
    auto convert_options = arrow::csv::ConvertOptions::Defaults();

    auto reader_res = arrow::csv::TableReader::Make(
        arrow::default_memory_pool(),
        input,
        read_options,
        parse_options,
        convert_options
    );
    if (!reader_res.ok()) {
        io_fail("read_csv", "failed to create CSV reader for '" + path + "': " + reader_res.status().ToString());
    }
    std::shared_ptr<arrow::csv::TableReader> reader = *std::move(reader_res);

    auto table_res = reader->Read();
    if (!table_res.ok()) {
        io_fail("read_csv", "failed to parse '" + path + "': " + table_res.status().ToString());
    }

    auto table = *std::move(table_res);
    validate_table_strict(table);
    return from_arrow_table(std::move(table));
}

EagerDataFrame read_parquet(const std::string& path) {
    auto input_res = arrow::io::ReadableFile::Open(path);
    if (!input_res.ok()) {
        io_fail("read_parquet", "failed to open '" + path + "': " + input_res.status().ToString());
    }
    std::shared_ptr<arrow::io::ReadableFile> input = *std::move(input_res);

    std::shared_ptr<parquet::arrow::FileReader> parquet_reader;
    auto open_status = parquet::arrow::OpenFile(
        input,
        arrow::default_memory_pool(),
        &parquet_reader
    );
    check_arrow_status(open_status, "read_parquet");

    std::shared_ptr<arrow::Table> table;
    auto read_status = parquet_reader->ReadTable(&table);
    check_arrow_status(read_status, "read_parquet");

    validate_table_strict(table);
    return from_arrow_table(std::move(table));
}

void write_csv(const EagerDataFrame& df, const std::string& path) {
    auto table = to_arrow_table(df);

    auto output_res = arrow::io::FileOutputStream::Open(path);
    if (!output_res.ok()) {
        io_fail("write_csv", "failed to open '" + path + "': " + output_res.status().ToString());
    }
    std::shared_ptr<arrow::io::FileOutputStream> output = *std::move(output_res);

    arrow::csv::WriteOptions write_options = arrow::csv::WriteOptions::Defaults();
    write_options.null_string = "";

    auto status = arrow::csv::WriteCSV(*table, write_options, output.get());
    check_arrow_status(status, "write_csv");

    auto close_status = output->Close();
    check_arrow_status(close_status, "write_csv");
}

void write_parquet(const EagerDataFrame& df, const std::string& path) {
    auto table = to_arrow_table(df);

    auto output_res = arrow::io::FileOutputStream::Open(path);
    if (!output_res.ok()) {
        io_fail("write_parquet", "failed to open '" + path + "': " + output_res.status().ToString());
    }
    std::shared_ptr<arrow::io::FileOutputStream> output = *std::move(output_res);

    auto status = parquet::arrow::WriteTable(
        *table,
        arrow::default_memory_pool(),
        output,
        1024
    );
    check_arrow_status(status, "write_parquet");

    auto close_status = output->Close();
    check_arrow_status(close_status, "write_parquet");
}