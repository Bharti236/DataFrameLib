class EagerDataFrame {
    // ???

    // “Use Apache Arrow for all data storage” => 
    // DataFrame should internally store - Arrow Table
    // Helps in 1. type safety 2. columnar format

};

// ------------------------------------------------------------
// src/Eager.cpp
// ------------------------------------------------------------
//
// This file should contain the execution logic for all eager operations.
//
// It should implement:
//
// - DataFrame validation on construction
// - select by column names
// - select by expressions
// - filter
// - with_column
// - group_by helper construction
// - grouped aggregate execution
// - join
// - sort
// - head
// - writer forwarding helpers
//
// Core implementation rule:
// Every method must return a fresh EagerDataFrame.
// No method should modify the current object's table in place.
//
// Null semantics to preserve here:
// - Expression evaluation must not use NaN as a missing-value marker.
// - Any operation involving a null operand should generate a null result.
// - Aggregations should ignore nulls where mathematically appropriate,
//   but preserve nulls when a group has no valid input.
//
// Performance note:
// - Do not copy whole tables more than necessary.
// - Prefer Arrow slices, views, and builders where possible.
// - Use move semantics for newly built tables when safe.