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

    // Map global state IDs to local indices 0..N
    std::vector<int> sortedStateIds;
    for (const auto& pair : nfa.states) {
        sortedStateIds.push_back(pair.first);
    }
    std::sort(sortedStateIds.begin(), sortedStateIds.end());

    std::map<int, int> globalToLocal;
    int localIdx = 0;
    globalToLocal[nfa.startStateId] = localIdx++;
    for (int id : sortedStateIds) {
        if (id != nfa.startStateId) {
            globalToLocal[id] = localIdx++;
        }
    }

    int numStates = static_cast<int>(nfa.states.size());
    int startLocalId = 0;

    out << "// NFA for regex index " << nfa.regexIndex << "\n";
    out << "module nfa_" << nfa.regexIndex << " (\n";
    out << "    input  wire       clk,\n";
    out << "    input  wire       rst,\n";
    out << "    input  wire       start,\n";
    out << "    input  wire       end_of_str,\n";
    out << "    input  wire [7:0] char_in,\n";
    out << "    output reg        match\n";
    out << ");\n\n";

    out << "    reg [" << numStates - 1 << ":0] state_reg;\n";
    out << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    for (int j = 0; j < numStates; ++j) {
        int targetGlobalId = -1;
        for (const auto& pair : globalToLocal) {
            if (pair.second == j) { targetGlobalId = pair.first; break; }
        }
        
        std::map<unsigned char, std::vector<int>> incoming;
        for (const auto& [srcGlobalId, srcState] : nfa.states) {
            for (const auto& [c, dstIds] : srcState.transitions) {
                if (dstIds.count(targetGlobalId)) {
                    incoming[c].push_back(globalToLocal[srcGlobalId]);
                }
            }
        }

        out << "    assign next_state[" << j << "] = ";
        if (incoming.empty()) {
            out << "1'b0;\n";
        } else if (incoming.size() == 256) {
            out << "(";
            auto& srcs = incoming.begin()->second;
            for (size_t k = 0; k < srcs.size(); ++k) {
                out << "state_reg[" << srcs[k] << "]";
                if (k < srcs.size() - 1) out << " | ";
            }
            out << "); // DOT transition\n";
        } else {
            bool firstChar = true;
            for (const auto& pair : incoming) {
                if (!firstChar) out << " ||\n                        ";
                out << "(char_in == 8'h" << std::hex << std::setw(2) << std::setfill('0') << (int)pair.first << std::dec 
                    << " && (";
                for (size_t k = 0; k < pair.second.size(); ++k) {
                    out << "state_reg[" << pair.second[k] << "]";
                    if (k < pair.second.size() - 1) out << " | ";
                }
                out << "))";
                firstChar = false;
            }
            out << ";\n";
        }
    }

    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            state_reg <= " << numStates << "'b0;\n"
        << "            state_reg[" << startLocalId << "] <= 1'b1;\n"
        << "        end else begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (end_of_str) begin\n"
        << "            match <= ";
    
    std::vector<std::string> acceptTerms;
    for (const auto& [id, state] : nfa.states) {
        if (state.isAccept) {
            if (!firstAccept) out << " || ";
            out << "next_state[" << globalToLocal[id] << "]";
            firstAccept = false;
        }
    }
    if (firstAccept) out << "1'b0";
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
            << "        .clk(clk), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[" << nfas[i]->regexIndex << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

void Emitter::emitTestbench(const std::string& outputDir, 
                           const std::vector<std::string>& testStrings, 
                           const std::vector<std::string>& expectedMatches) const {
    std::string filename = outputDir + "/tb_top.v";
    std::ofstream out(filename);

    out << "`timescale 1ns / 1ps\n\n"
        << "module tb_top;\n"
        << "    reg clk, rst, start, end_of_str;\n"
        << "    reg [7:0] char_in;\n"
        << "    wire [" << (nfas.empty() ? 0 : nfas.size() - 1) << ":0] match_bus;\n\n"
        << "    top uut (.clk(clk), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match_bus(match_bus));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    initial begin\n"
        << "        $dumpfile(\"dump.vcd\");\n"
        << "        $dumpvars(0, tb_top);\n"
        << "        clk = 0; rst = 1; start = 0; end_of_str = 0; char_in = 0;\n"
        << "        #20 rst = 0; #10;\n\n";

    for (size_t i = 0; i < testStrings.size(); ++i) {
        const std::string& s = testStrings[i];
        out << "        // Test case " << i << ": \"" << s << "\"\n"
            << "        start = 1; #10 start = 0;\n";
        
        if (s.empty()) {
            out << "        end_of_str = 1; #10 end_of_str = 0;\n";
        } else {
            for (size_t j = 0; j < s.length(); ++j) {
                out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)s[j] << std::dec << "; ";
                if (j == s.length() - 1) out << "end_of_str = 1; ";
                out << "#10; ";
                if (j == s.length() - 1) out << "end_of_str = 0; ";
                out << "\n";
            }
        }
        
        out << "        #10;\n";
        if (i < expectedMatches.size()) {
            out << "        if (match_bus === " << nfas.size() << "'b" << expectedMatches[i] << ") "
                << "$display(\"PASS: Test case " << i << " ('" << s << "') matches expected mask " << expectedMatches[i] << "\");\n"
                << "        else "
                << "$display(\"FAIL: Test case " << i << " ('" << s << "') expected " << expectedMatches[i] << ", got %b\", match_bus);\n\n";
        } else {
             out << "        $display(\"Result for '" << s << "': %b\", match_bus);\n\n";
        }
    }

    out << "        #100; $finish;\n    end\nendmodule\n";
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