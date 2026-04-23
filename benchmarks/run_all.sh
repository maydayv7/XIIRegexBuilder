#!/bin/bash

# Ensure directory and binaries
mkdir -p build
g++ -Wall -Wextra -std=c++17 -Isrc -o benchmarks/bench_cpp benchmarks/bench_cpp.cpp

LARGE_TEST="inputs/large_test_strings.txt"
REGEX_FILE="inputs/regexes.txt"
NUM_REGEX=$(grep -v "^#" "$REGEX_FILE" | grep -v "^$" | wc -l | tr -d ' ')

echo "===================================================="
echo "      XIIRegexBuilder: MASSIVE STRESS TEST         "
echo "===================================================="
echo "Dataset: 10,000 strings"
echo "Patterns: $NUM_REGEX regexes"
echo ""

# 1. Python
python3 benchmarks/bench_python.py "$REGEX_FILE" "$LARGE_TEST"
echo ""

# 2. C++
./benchmarks/bench_cpp "$REGEX_FILE" "$LARGE_TEST"
echo ""

# 3. Hardware Timing Logic
TOTAL_CHARS=0
TOTAL_STRINGS=0
while IFS= read -r line || [ -n "$line" ]; do
    [[ "$line" =~ ^# ]] && continue
    [[ -z "${line// }" ]] && continue
    s=$(echo -n "$line" | tr -d '\r\n')
    TOTAL_CHARS=$((TOTAL_CHARS + ${#s}))
    TOTAL_STRINGS=$((TOTAL_STRINGS + 1))
done < "$LARGE_TEST"

# Logic cycles: 1(start) + N(chars) + 1(end) per string
TOTAL_CYCLES=$((TOTAL_CHARS + (TOTAL_STRINGS * 2)))
HW_TIME_NS=$((TOTAL_CYCLES * 10))
HW_TIME_MS=$(echo "scale=4; $HW_TIME_NS / 1000000" | bc)

# UART Bottleck: 115200 baud is approx 11,520 bytes/sec
# Each string has START(1) + DATA(N) + END(1) + RESPONSE(2) = N+4 bytes
TOTAL_BYTES=$((TOTAL_CHARS + (TOTAL_STRINGS * 4)))
UART_TIME_SEC=$(echo "scale=4; $TOTAL_BYTES / 11520" | bc)

echo "XIIRegexBuilder (NFA Hardware Analysis):"
echo "  [INTERNAL] Total Clock Cycles: $TOTAL_CYCLES (@ 100MHz)"
echo "  [INTERNAL] Theoretical Match Time: $HW_TIME_MS milliseconds"
echo "  [EXTERNAL] Est. UART Transmission: $UART_TIME_SEC seconds (@ 115200)"
echo ""

echo "===================================================="
echo "               Ready for FPGA Connection           "
echo "===================================================="
echo "To run on physical hardware, connect FPGA and run:"
echo "python3 benchmarks/bench_fpga_uart.py"
echo "===================================================="
