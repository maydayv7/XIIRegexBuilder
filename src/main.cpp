#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <regex>
#include "lexer.h"
#include "parser.h"
#include "nfa.h"
#include "emitter.h"

std::string trimLine(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <regex_file> [output_dir] [test_strings_file]" << std::endl;
        return 1;
    }

    std::string regexFilename = argv[1];
    std::string outputDir = (argc > 2) ? argv[2] : "output";
    std::string testFilename = (argc > 3) ? argv[3] : "";

    std::ifstream regexFile(regexFilename);
    if (!regexFile.is_open()) {
        std::cerr << "Failed to open regex file: " << regexFilename << std::endl;
        return 1;
    }

    std::vector<std::string> testStrings;
    if (!testFilename.empty()) {
        std::ifstream testFile(testFilename);
        if (testFile.is_open()) {
            std::string tLine;
            while (std::getline(testFile, tLine)) {
                std::string trimmed = trim(tLine);
                if (trimmed.empty() || trimmed[0] == '#') continue;
                testStrings.push_back(trimmed);
            }
        } else {
            std::cerr << "Warning: Could not open test strings file: " << testFilename << std::endl;
        }
    }

    std::string line;
    int lineNum = 0;
    int regexIdx = 0;
    std::vector<std::unique_ptr<NFA>> nfas;
    std::vector<std::string> rawRegexes;

    std::cout << "Starting XIIRegexBuilder pipeline..." << std::endl;

    while (std::getline(regexFile, line)) {
        lineNum++;
        std::string trimmedLine = trimLine(line);
        if (trimmedLine.empty() || trimmedLine[0] == '#') continue;

        rawRegexes.push_back(trimmedLine);
        std::cout << "Processing Regex [" << regexIdx << "]: " << trimmedLine << std::endl;

        Lexer lexer(trimmedLine, lineNum);
        try {
            std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens);
            auto ast = parser.parse();
            auto nfa = NFABuilder::build(ast.get(), regexIdx++);
            if (nfa) {
                nfas.push_back(std::move(nfa));
            }
        } catch (const std::exception& e) {
            std::cerr << "Error parsing regex: " << e.what() << std::endl;
        }
    }

    if (nfas.empty()) {
        std::cerr << "No valid regexes found. Exiting." << std::endl;
        return 1;
    }

    std::vector<std::string> testStrings;
    std::vector<std::string> expectedMatches;

    if (!testFilename.empty()) {
        std::ifstream testFile(testFilename);
        if (testFile.is_open()) {
            std::string tline;
            while (std::getline(testFile, tline)) {
                size_t last = tline.find_last_not_of("\r\n");
                std::string s = (last != std::string::npos) ? tline.substr(0, last + 1) : (tline.empty() ? "" : tline);
                testStrings.push_back(s);
                
                // Generate expected matches using std::regex
                std::string mask = "";
                for (const auto& re_str : rawRegexes) {
                    bool match = false;
                    try {
                        std::regex re(re_str);
                        match = std::regex_match(s, re);
                    } catch (...) {}
                    mask += (match ? "1" : "0");
                }
                // Reverse mask for Verilog indexing (bit 0 is first regex)
                std::reverse(mask.begin(), mask.end());
                expectedMatches.push_back(mask);
            }
            std::cout << "Loaded " << testStrings.size() << " test strings and generated golden matches." << std::endl;
        }
    }

    std::cout << "Generated " << nfas.size() << " NFAs. Emitting Verilog..." << std::endl;

    Emitter emitter(nfas);
    emitter.emit(outputDir, testStrings, expectedMatches);

    std::cout << "Pipeline complete." << std::endl;

    return 0;
}