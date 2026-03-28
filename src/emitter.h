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
    
    /**
     * @brief Emits all Verilog files (nfa_*.v, top.v, tb_top.v) to the specified directory.
     * @param outputDir Directory where files will be written.
     */
    void emit(const std::string& outputDir, 
              const std::vector<std::string>& testStrings = {}, 
              const std::vector<std::string>& expectedMatches = {}) const;

private:
    const std::vector<std::unique_ptr<NFA>>& nfas;
    std::vector<TestCase> testCases;

    void emitNFAModule(const NFA& nfa, const std::string& outputDir) const;
    void emitTopModule(const std::string& outputDir) const;
    void emitTestbench(const std::string& outputDir, 
                       const std::vector<std::string>& testStrings, 
                       const std::vector<std::string>& expectedMatches) const;

    // Helper to ensure output directory exists
    static void ensureDirectory(const std::string& dir);
    
    // Helper to escape characters for Verilog comments/literals
    static std::string escapeChar(unsigned char c);
};

#endif // EMITTER_H