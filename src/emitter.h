#ifndef EMITTER_H
#define EMITTER_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

#include <filesystem>
#include <system_error>

class Emitter {
public:
    Emitter() = delete; // Static class, no instances

    /**
     * @brief Emits all Verilog files (nfa_*.v, top.v, tb_top.v) to the specified directory.
     * @param nfas The vector of NFAs to be emitted.
     * @param outputDir Directory where files will be written.
     * @param testStrings Optional test strings for the testbench.
     * @param expectedMatches Optional expected match masks for the testbench.
     */
    static void emit(const std::vector<std::unique_ptr<NFA>>& nfas,
                     const std::string& outputDir, 
                     const std::vector<std::string>& testStrings = {}, 
                     const std::vector<std::string>& expectedMatches = {});

private:
    static void emitNFAModule(const NFA& nfa, const std::filesystem::path& outputDir);
    static void emitTopModule(const std::vector<std::unique_ptr<NFA>>& nfas, const std::filesystem::path& outputDir);
    static void emitTestbench(const std::vector<std::unique_ptr<NFA>>& nfas,
                              const std::filesystem::path& outputDir, 
                              const std::vector<std::string>& testStrings, 
                              const std::vector<std::string>& expectedMatches);
};

#endif // EMITTER_H
