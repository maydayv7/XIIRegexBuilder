#!/bin/bash
# XIIRegexBuilder — Full Ethernet Benchmark Suite

mkdir -p build benchmarks/results
REGEX_FILE="inputs/regexes.txt"
LARGE_TEST="inputs/large_test_strings.txt"

# 1. Compile Tools
echo "Compiling benchmark tools..."
g++ -O3 -std=c++17 -Isrc -o benchmarks/bench_fpga_eth benchmarks/bench_fpga_eth.cpp
g++ -O2 -std=c++17 -Isrc -o benchmarks/bench_cpp benchmarks/bench_cpp.cpp

echo "===================================================="
echo "      XIIRegexBuilder: ETHERNET STRESS TEST        "
echo "===================================================="

# 2. Software Baselines
python3 benchmarks/bench_python.py "$REGEX_FILE" "$LARGE_TEST"
./benchmarks/bench_cpp "$REGEX_FILE" "$LARGE_TEST"

# 3. Ethernet Benchmarks (if connected)
echo ""
echo "Attempting FPGA Ethernet Benchmark (Python)..."
python3 benchmarks/bench_fpga_eth.py --input "$LARGE_TEST" --regex "$REGEX_FILE"

echo ""
echo "Attempting FPGA Ethernet Benchmark (C++)..."
./benchmarks/bench_fpga_eth "$LARGE_TEST"

echo ""
echo "===================================================="
echo "Benchmark Suite Complete."
echo "===================================================="
