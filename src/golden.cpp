#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <iomanip>

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
        // Test strings might have spaces, but usually they are one per line.
        // We trim only trailing \r\n to preserve leading/trailing spaces if they are part of the test.
        size_t last = line.find_last_not_of("\r\n");
        if (last != std::string::npos) {
            testStrings.push_back(line.substr(0, last + 1));
        } else if (!line.empty()) {
             testStrings.push_back("");
        } else {
             // Blank line in test file? Spec says "Each line is one test string".
             // We'll treat blank lines as empty strings.
             testStrings.push_back("");
        }
    }
    testFile.close();

    std::ofstream outFile(outputFilename);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open " << outputFilename << " for writing." << std::endl;
        return 1;
    }

    for (const auto& testStr : testStrings) {
        for (size_t i = 0; i < regexes.size(); ++i) {
            bool matches = false;
            try {
                // Convert our regex syntax to ECMAScript (default for std::regex)
                // Our syntax is mostly compatible, but '.' matches any char including \n in some engines.
                // std::regex_match does full match.
                std::regex re(regexes[i]);
                matches = std::regex_match(testStr, re);
            } catch (const std::regex_error& e) {
                // If std::regex doesn't like it, we might have a syntax mismatch.
                // But for standard operators like *, +, ?, |, (), it should be fine.
            }
            outFile << (matches ? "1" : "0");
        }
        outFile << "\n";
    }
    outFile.close();

    std::cout << "Golden reference generated: " << outputFilename << std::endl;

    return 0;
}