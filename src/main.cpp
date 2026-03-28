#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include "lexer.h"
#include "parser.h"
#include "nfa.h"
#include "emitter.h"

std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <regex_file> [test_strings_file] [output_dir]" << std::endl;
        std::cerr << "Note: Use '-' for [test_strings_file] if you only want to specify [output_dir]" << std::endl;
        return 1;
    }

    std::string regexFilename = argv[1];
    std::string testFilename = (argc > 2) ? argv[2] : "";
    std::string outputDir = (argc > 3) ? argv[3] : "output";

    // Handle the case where the user wants to skip test strings but provide output dir
    if (testFilename == "-" || testFilename == "none") {
        testFilename = "";
    }

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

    std::cout << "Starting XIIRegexBuilder pipeline..." << std::endl;

    while (std::getline(regexFile, line)) {
        lineNum++;
        std::string trimmedLine = trim(line);
        if (trimmedLine.empty() || trimmedLine[0] == '#') continue;

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

    std::cout << "Generated " << nfas.size() << " NFAs. Emitting Verilog..." << std::endl;

    Emitter emitter(nfas);
    
    // Generate expected matches for the testbench if test strings were provided
    for (const auto& ts : testStrings) {
        std::vector<bool> expected;
        for (const auto& nfa : nfas) {
            expected.push_back(nfa->simulate(ts));
        }
        emitter.addTestCase(ts, expected);
    }

    try {
        emitter.emit(outputDir);
        std::cout << "Verilog files emitted to '" << outputDir << "/'" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Emission failed: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Pipeline complete." << std::endl;

    return 0;
}
