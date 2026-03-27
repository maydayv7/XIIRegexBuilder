#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <iomanip>
#include <algorithm>

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <regex_file> <test_strings_file> [output_file]" << std::endl;
        return 1;
    }

    std::string regexFilename = argv[1];
    std::string testFilename = argv[2];
    std::string outputFilename = (argc > 3) ? argv[3] : "expected_matches.txt";

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
        // Remove trailing \r\n
        size_t last = line.find_last_not_of("\r\n");
        std::string s = (last != std::string::npos) ? line.substr(0, last + 1) : (line.empty() ? "" : line);
        
        // Skip comments and blank lines if desired, but spec says "Each line is one test string"
        // Let's at least skip the comment headers if they start with #
        if (!s.empty() && s[0] == '#') continue;
        if (s.empty()) {
            // Check if it's truly a blank line (might be intentional empty string test)
            // For safety, let's only skip if the raw line was empty or just whitespace
            if (trim(line).empty()) continue;
        }

        testStrings.push_back(s);
    }
    testFile.close();

    std::ofstream outFile(outputFilename);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << outputFilename << " for writing." << std::endl;
        return 1;
    }

    for (const auto& testStr : testStrings) {
