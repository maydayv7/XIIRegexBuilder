import re
import time
import os
import sys

def trim(s):
    return s.strip()

def run_benchmark(regex_file, test_file):
    if not os.path.exists(regex_file) or not os.path.exists(test_file):
        print(f"Error: Files not found.")
        return

    with open(regex_file, 'r') as f:
        regexes = [trim(line) for line in f if trim(line) and not trim(line).startswith('#')]

    with open(test_file, 'r') as f:
        test_strings = [line.rstrip('\r\n') for line in f if line.strip() and not line.strip().startswith('#')]

    # Compilation phase (not timed as part of the match loop)
    compiled_regexes = []
    for r in regexes:
        try:
            compiled_regexes.append(re.compile(r))
        except re.error:
            compiled_regexes.append(None)

    # Benchmark phase
    start_time = time.perf_counter_ns()
    
    results = []
    for s in test_strings:
        mask = ""
        for cregex in compiled_regexes:
            if cregex and cregex.fullmatch(s):
                mask += "1"
            else:
                mask += "0"
        # Reverse mask for bit 0 on right (consistency with golden.cpp)
        results.append(mask[::-1])
    
    end_time = time.perf_counter_ns()
    
    total_time_ns = end_time - start_time
    print(f"Python `re` Total Matching Time: {total_time_ns / 1000:.2f} microseconds")
    print(f"Number of test cases: {len(test_strings)}")
    
    # Save results to verify
    with open("python_matches.txt", "w") as f:
        for r in results:
            f.write(r + "\n")

if __name__ == "__main__":
    regex_f = sys.argv[1] if len(sys.argv) > 1 else "inputs/regexes.txt"
    test_f = sys.argv[2] if len(sys.argv) > 2 else "inputs/test_strings.txt"
    run_benchmark(regex_f, test_f)
