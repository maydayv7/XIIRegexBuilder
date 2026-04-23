#!/bin/bash
# XIIRegexBuilder — Full Ethernet Benchmark Suite

mkdir -p build benchmarks/results
ASSETS_DIR="benchmarks/assets"
REGEX_FILE="${ASSETS_DIR}/bench_regexes.txt"
STRINGS_FILE="${ASSETS_DIR}/bench_strings.txt"

# 1. Ensure Assets Exist
if [ ! -f "$STRINGS_FILE" ]; then
    echo "Benchmark assets not found. Generating default scale (70 regexes, 10,000 strings)..."
    python3 benchmarks/generate_benchmark_assets.py 70 10000
fi

NUM_REGEX=$(grep -v "^#" "$REGEX_FILE" | grep -v "^$" | wc -l | tr -d ' ')
NUM_STRINGS=$(grep -v "^#" "$STRINGS_FILE" | grep -v "^$" | wc -l | tr -d ' ')

# 2. Compile Tools
echo "Compiling benchmark tools..."
g++ -O3 -std=c++17 -Isrc -o benchmarks/bench_fpga_eth benchmarks/bench_fpga_eth.cpp
g++ -O2 -std=c++17 -Isrc -o benchmarks/bench_cpp benchmarks/bench_cpp.cpp

echo "===================================================="
echo "      XIIRegexBuilder: ETHERNET STRESS TEST        "
echo "===================================================="
echo "Scale: $NUM_REGEX patterns vs $NUM_STRINGS strings"
echo ""

# 3. Software Baselines
echo "--- Running Software Baselines ---"
python3 benchmarks/bench_python.py "$REGEX_FILE" "$STRINGS_FILE"
./benchmarks/bench_cpp "$REGEX_FILE" "$STRINGS_FILE"

# 4. Ethernet Benchmarks (if connected)
echo ""
echo "--- Attempting FPGA Ethernet Benchmark (Python) ---"
python3 benchmarks/bench_fpga_eth.py --input "$STRINGS_FILE" --regex "$REGEX_FILE"

echo ""
echo "--- Attempting FPGA Ethernet Benchmark (C++) ---"
./benchmarks/bench_fpga_eth "$STRINGS_FILE"

echo ""
echo "===================================================="
echo "Benchmark Suite Complete."
echo "===================================================="
