#include "emitter.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

Emitter::Emitter(const std::vector<std::unique_ptr<NFA>>& nfas) : nfas(nfas) {}

void Emitter::emit(const std::string& outputDir, 
                   const std::vector<std::string>& testStrings, 
                   const std::vector<std::string>& expectedMatches) const {
    ensureDirectory(outputDir);

    for (const auto& nfa : nfas) {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(outputDir);
    emitTestbench(outputDir, testStrings, expectedMatches);
}

void Emitter::emitNFAModule(const NFA& nfa, const std::string& outputDir) const {
    std::string filename = outputDir + "/nfa_" + std::to_string(nfa.regexIndex) + ".v";
    std::ofstream out(filename);
    if (!out.is_open()) {
        throw std::runtime_error("Could not open " + filename + " for writing.");
    }

    int numStates = static_cast<int>(nfa.states.size());
    
    std::map<int, int> globalToLocal;
    int localIdx = 0;
    globalToLocal[nfa.startStateId] = localIdx++;
    
    // Sort other state IDs for deterministic output
    std::set<int> otherIds;
    for (const auto& pair : nfa.states) {
        if (pair.first != nfa.startStateId) otherIds.insert(pair.first);
    }
    for (int id : otherIds) {
        globalToLocal[id] = localIdx++;
    }

    out << "// NFA for regex index " << nfa.regexIndex << "\n";
    out << "module nfa_" << nfa.regexIndex << " (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire       end_of_str,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output reg        match\n"
        << ");\n\n";

    out << "    // One-hot state register\n"
        << "    reg [" << numStates - 1 << ":0] state_reg;\n"
        << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    for (int j = 0; j < numStates; ++j) {
        out << "    assign next_state[" << j << "] = ";
        
        std::vector<std::string> terms;
        for (const auto& [srcGlobalId, srcState] : nfa.states) {
            int srcLocal = globalToLocal.at(srcGlobalId);
            for (const auto& [c, dstIds] : srcState.transitions) {
                for (int dstGlobalId : dstIds) {
                    if (globalToLocal.at(dstGlobalId) == j) {
                        terms.push_back("(state_reg[" + std::to_string(srcLocal) + "] && (char_in == 8'd" + std::to_string((int)c) + "))");
                    }
                }
            }
        }

        if (terms.empty()) {
            out << "1'b0;\n";
        } else if (terms.size() == 1) {
            out << terms[0] << ";\n";
        } else {
            out << "(\n";
            for (size_t t = 0; t < terms.size(); ++t) {
                out << "        " << terms[t];
                if (t < terms.size() - 1) out << " ||\n";
            }
            out << "\n    );\n";
        }
    }

    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            state_reg <= {" << (numStates > 1 ? "{" + std::to_string(numStates - 1) + "{1'b0}}, " : "") << "1'b1};\n"
        << "        end else begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n"
        << "    // Match logic: asserted on cycle following end_of_str\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (end_of_str) begin\n"
        << "            match <= ";
    
    std::vector<std::string> acceptTerms;
    for (const auto& [id, state] : nfa.states) {
        if (state.isAccept) {
            acceptTerms.push_back("state_reg[" + std::to_string(globalToLocal.at(id)) + "]");
        }
    }

    if (acceptTerms.empty()) {
        out << "1'b0";
    } else if (acceptTerms.size() == 1) {
        out << acceptTerms[0];
    } else {
        out << "(\n";
        for (size_t t = 0; t < acceptTerms.size(); ++t) {
            out << "                " << acceptTerms[t];
            if (t < acceptTerms.size() - 1) out << " ||\n";
        }
        out << "\n            )";
    }
    
    out << ";\n"
        << "        end else begin\n"
        << "            match <= 1'b0;\n"
        << "        end\n"
        << "    end\n\n"
        << "endmodule\n";
}

void Emitter::emitTopModule(const std::string& outputDir) const {
    std::string filename = outputDir + "/top.v";
    std::ofstream out(filename);
    if (!out.is_open()) throw std::runtime_error("Could not open top.v");

    out << "module top (\n"
        << "    input  wire        clk,\n"
        << "    input  wire        rst,\n"
        << "    input  wire        start,\n"
        << "    input  wire        end_of_str,\n"
        << "    input  wire [7:0]  char_in,\n"
        << "    output wire [" << (nfas.empty() ? 0 : nfas.size() - 1) << ":0] match_bus\n"
        << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i) {
        out << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
            << "        .clk(clk), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[" << i << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

void Emitter::emitTestbench(const std::string& outputDir, 
                           const std::vector<std::string>& testStrings, 
                           const std::vector<std::string>& expectedMatches) const {
    std::string filename = outputDir + "/tb_top.v";
    std::ofstream out(filename);
    if (!out.is_open()) throw std::runtime_error("Could not open tb_top.v");

    int numNFAs = static_cast<int>(nfas.size());

    out << "`timescale 1ns / 1ps\n\n"
        << "module tb_top;\n"
        << "    reg clk, rst, start, end_of_str;\n"
        << "    reg [7:0] char_in;\n"
        << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
        << "    top uut (.clk(clk), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match_bus(match_bus));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    initial begin\n"
        << "        // synthesis translate_off\n"
        << "        `ifndef SYNTHESIS\n"
        << "        $dumpfile(\"dump.vcd\");\n"
        << "        $dumpvars(0, tb_top);\n"
        << "        `endif\n"
        << "        // synthesis translate_on\n\n"
        << "        clk = 0; rst = 1; start = 0; end_of_str = 0; char_in = 0;\n"
        << "        #20 rst = 0; #10;\n\n";

    for (size_t i = 0; i < testStrings.size(); ++i) {
        const std::string& s = testStrings[i];
        out << "        // Test case " << i << ": \"" << s << "\"\n"
            << "        start = 1; #10 start = 0;\n";
        
        for (size_t j = 0; j < s.length(); ++j) {
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)s[j] << std::dec << "; #10;\n";
        }
        
        out << "        end_of_str = 1; #10 end_of_str = 0;\n"
            << "        #10; // Registered delay\n";

        if (i < expectedMatches.size()) {
            out << "        if (match_bus === " << numNFAs << "'b" << expectedMatches[i] << ") "
                << "$display(\"PASS: Test case " << i << " ('" << s << "') matches expected mask " << expectedMatches[i] << "\");\n"
                << "        else "
                << "$display(\"FAIL: Test case " << i << " ('" << s << "') expected " << expectedMatches[i] << ", got %b\", match_bus);\n\n";
        } else {
             out << "        $display(\"Result for '" << s << "': %b\", match_bus);\n\n";
        }
    }

    out << "        $display(\"All tests completed.\");\n"
        << "        #100; $finish;\n    end\nendmodule\n";
}

void Emitter::ensureDirectory(const std::string& dir) {
    struct stat info;
    if (stat(dir.c_str(), &info) != 0) {
#ifdef _WIN32
        _mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0777);
#endif
    }
}

std::string Emitter::escapeChar(unsigned char c) {
    if (c >= 32 && c <= 126 && c != '"' && c != '\\') return std::string(1, (char)c);
    std::stringstream ss;
    ss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)c;
    return ss.str();
}
