#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <chrono>
#include <algorithm>

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

int main(int argc, char* argv[]) {
    std::string regexFilename = (argc > 1) ? argv[1] : "inputs/regexes.txt";
    std::string testFilename = (argc > 2) ? argv[2] : "inputs/test_strings.txt";

    std::ifstream regexFile(regexFilename);
    if (!regexFile.is_open()) {
        std::cerr << "Failed to open " << regexFilename << std::endl;
        return 1;
    }

    std::vector<std::string> regexes;
    std::string line;
    while (std::getline(regexFile, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;
        regexes.push_back(trimmed);
    }
    regexFile.close();

    std::ifstream testFile(testFilename);
    if (!testFile.is_open()) {
        std::cerr << "Failed to open " << testFilename << std::endl;
        return 1;
    }

    std::vector<std::string> testStrings;
    while (std::getline(testFile, line)) {
        size_t last = line.find_last_not_of("\r\n");
        std::string s = (last != std::string::npos) ? line.substr(0, last + 1) : (line.empty() ? "" : line);
        if (!s.empty() && s[0] == '#') continue;
        if (trim(line).empty()) continue;
        testStrings.push_back(s);
    }
    testFile.close();

    // Compilation phase
    std::vector<std::regex> compiledRegexes;
    for (const auto& r : regexes) {
        try {
            compiledRegexes.emplace_back(r);
        } catch (const std::regex_error&) {
            // Placeholder for invalid regex
        }
    }

    // Benchmark phase
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::string> results;
    for (const auto& testStr : testStrings) {
        std::string mask = "";
        for (const auto& re : compiledRegexes) {
            bool matches = std::regex_match(testStr, re);
            mask += (matches ? "1" : "0");
        }
        std::reverse(mask.begin(), mask.end());
        results.push_back(mask);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    std::cout << "C++ `std::regex` Total Matching Time: " << duration.count() / 1000.0 << " microseconds" << std::endl;
    std::cout << "Number of test cases: " << testStrings.size() << std::endl;

    std::ofstream outFile("cpp_matches.txt");
    for (const auto& res : results) {
        outFile << res << "\n";
    }
    outFile.close();

    return 0;
}
