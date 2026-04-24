#!/usr/bin/env python3
"""Generate reproducible test data for DataFrameLib autograder."""

import os
import random
import csv
import argparse

import pyarrow as pa
import pyarrow.csv as pcsv
import pyarrow.parquet as pq


SEED = 42

DEPARTMENTS = ["Engineering", "Sales", "Marketing", "HR", "Finance"]
CITIES = ["NYC", "SF", "LA", "Chicago", "Seattle", "Austin", "Boston", "Denver"]
FIRST_NAMES = [
    "Alice", "Bob", "Charlie", "Diana", "Eve", "Frank", "Grace", "Hank",
    "Iris", "Jack", "Karen", "Leo", "Mona", "Nick", "Olivia", "Paul",
    "Quinn", "Rita", "Sam", "Tina", "Uma", "Vic", "Wendy", "Xander",
    "Yara", "Zane",
]
LAST_NAMES = [
    "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller",
    "Davis", "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez",
    "Wilson", "Anderson", "Thomas", "Taylor", "Moore", "Jackson", "Martin",
]
EMAIL_DOMAINS = ["@gmail.com", "@yahoo.com", "@company.com", "@outlook.com"]
CODE_PREFIXES = ["A", "B", "C", "D"]
FILE_EXTENSIONS = [".txt", ".csv", ".pdf", ".docx"]
CATEGORIES = ["cat_A", "cat_B", "cat_C", "cat_D", "cat_E"]


def generate_name(rng: random.Random) -> str:
    return f"{rng.choice(FIRST_NAMES)} {rng.choice(LAST_NAMES)}"


def generate_data_csv(output_dir: str, n: int = 1000) -> None:
    rng = random.Random(SEED)
    path = os.path.join(output_dir, "data.csv")
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "name", "age", "salary", "department", "city"])
        for i in range(1, n + 1):
            writer.writerow([
                i,
                generate_name(rng),
                rng.randint(22, 65),
                round(rng.uniform(30000, 150000), 2),
                rng.choice(DEPARTMENTS),
                rng.choice(CITIES),
            ])
    print(f"  Generated {path} ({n} rows)")


def generate_join_csvs(output_dir: str, n: int = 500) -> None:
    rng = random.Random(SEED + 1)
    all_ids = list(range(1, n + 200 + 1))
    rng.shuffle(all_ids)

    left_ids = sorted(all_ids[:n])
    overlap_count = int(n * 0.6)
    right_ids_from_left = rng.sample(left_ids, overlap_count)
    non_overlap_count = n - overlap_count
    remaining_ids = [x for x in all_ids[n:] if x not in set(left_ids)]
    right_ids_unique = remaining_ids[:non_overlap_count]
    right_ids = sorted(right_ids_from_left + right_ids_unique)

    left_path = os.path.join(output_dir, "left.csv")
    with open(left_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "name", "value_left"])
        for lid in left_ids:
            writer.writerow([lid, generate_name(rng), round(rng.uniform(0, 1000), 2)])
    print(f"  Generated {left_path} ({len(left_ids)} rows)")

    right_path = os.path.join(output_dir, "right.csv")
    with open(right_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "category", "value_right"])
        for rid in right_ids:
            writer.writerow([rid, rng.choice(CATEGORIES), round(rng.uniform(0, 500), 2)])
    print(f"  Generated {right_path} ({len(right_ids)} rows)")


def generate_string_data_csv(output_dir: str, n: int = 200) -> None:
    rng = random.Random(SEED + 2)
    path = os.path.join(output_dir, "string_data.csv")
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["name", "email", "city", "code", "filename"])
        for _ in range(n):
            name = generate_name(rng)
            email_user = name.split()[0].lower() + str(rng.randint(1, 999))
            email = email_user + rng.choice(EMAIL_DOMAINS)
            city = rng.choice(CITIES)
            code = rng.choice(CODE_PREFIXES) + str(rng.randint(100, 999))
            filename = f"file_{rng.randint(1, 500)}{rng.choice(FILE_EXTENSIONS)}"
            writer.writerow([name, email, city, code, filename])
    print(f"  Generated {path} ({n} rows)")


def generate_null_data_csv(output_dir: str, n: int = 100) -> None:
    rng = random.Random(SEED + 3)
    path = os.path.join(output_dir, "null_data.csv")
    null_rate = 0.15
    labels = ["alpha", "beta", "gamma", "delta"]
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "x", "y", "label"])
        for i in range(1, n + 1):
            x = "" if rng.random() < null_rate else round(rng.uniform(-100, 100), 2)
            y = "" if rng.random() < null_rate else round(rng.uniform(-100, 100), 2)
            label = "" if rng.random() < null_rate else rng.choice(labels)
            writer.writerow([i, x, y, label])
    print(f"  Generated {path} ({n} rows)")


def generate_large_data_csv(output_dir: str, n: int = 100000) -> None:
    rng = random.Random(SEED + 4)
    keys = [f"key_{i}" for i in range(50)]
    path = os.path.join(output_dir, "large_data.csv")
    with open(path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["id", "key", "value1", "value2", "value3", "category"])
        for i in range(1, n + 1):
            writer.writerow([
                i,
                rng.choice(keys),
                round(rng.uniform(-10000, 10000), 4),
                round(rng.uniform(-10000, 10000), 4),
                round(rng.uniform(-10000, 10000), 4),
                rng.choice(CATEGORIES),
            ])
    print(f"  Generated {path} ({n} rows)")


def generate_parquet(output_dir: str) -> None:
    csv_path = os.path.join(output_dir, "data.csv")
    parquet_path = os.path.join(output_dir, "data.parquet")
    table = pcsv.read_csv(csv_path)
    pq.write_table(
        table, parquet_path,
        version="1.0",
        data_page_version="1.0",
        write_statistics=False,
        store_schema=False,
    )
    print(f"  Generated {parquet_path}")


def generate_all(output_dir: str) -> None:
    os.makedirs(output_dir, exist_ok=True)
    print("Generating test data...")
    generate_data_csv(output_dir)
    generate_join_csvs(output_dir)
    generate_string_data_csv(output_dir)
    generate_null_data_csv(output_dir)
    generate_large_data_csv(output_dir)
    generate_parquet(output_dir)
    print("Data generation complete.")


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate test data for DataFrameLib autograder")
    parser.add_argument("--output-dir", default="test_data", help="Directory to write test data")
    args = parser.parse_args()
    generate_all(args.output_dir)


if __name__ == "__main__":
    main()
