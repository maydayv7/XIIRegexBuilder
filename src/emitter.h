#ifndef EMITTER_H
#define EMITTER_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

struct TestCase {
    std::string input;
    std::vector<bool> expectedMatches;
};

class Emitter {
public:
    explicit Emitter(const std::vector<std::unique_ptr<NFA>>& nfas);
    
    // Add test cases for the functional testbench
    void addTestCase(const std::string& input, const std::vector<bool>& matches);
    
    void emit(const std::string& outputDir);

private:
    const std::vector<std::unique_ptr<NFA>>& nfas;
    std::vector<TestCase> testCases;

    void emitNFAModule(const NFA& nfa, const std::string& outputDir);
    void emitTopModule(const std::string& outputDir);
    void emitTestbench(const std::string& outputDir);
};

#endif // EMITTER_H
