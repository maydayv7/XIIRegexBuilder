#include "emitter.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <system_error>
#include <filesystem>

void Emitter::emit(const std::vector<std::unique_ptr<NFA>>& nfas,
                   const std::string& outputDirStr, 
                   const std::vector<std::string>& testStrings, 
                   const std::vector<std::string>& expectedMatches) {
    if (nfas.empty()) {
        std::cout << "No valid NFAs were provided; skipping Verilog emission." << std::endl;
        return;
    }

    std::filesystem::path outputDir(outputDirStr);
    try {
        std::filesystem::create_directories(outputDir);
    } catch (const std::filesystem::filesystem_error& e) {
        throw std::system_error(e.code(), "Failed to create output directory: " + outputDir.string());
    }

    for (const auto& nfa : nfas) {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(nfas, outputDir);
    emitTestbench(nfas, outputDir, testStrings, expectedMatches);
}

void Emitter::emitNFAModule(const NFA& nfa, const std::filesystem::path& outputDir) {
    auto filePath = outputDir / ("nfa_" + std::to_string(nfa.regexIndex) + ".v");
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try {
        out.open(filePath);
    } catch (const std::ios_base::failure& e) {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    int numStates = static_cast<int>(nfa.states.size());
    if (numStates == 0) return; // Should not happen with valid NFAs

    std::map<int, int> globalToLocal;
    int localIdx = 0;
    
    // Ensure start state is always local state 0
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

    // Build an inverted transition map for efficient lookup: dst -> list of (src, char)
    std::map<int, std::vector<std::pair<int, unsigned char>>> invertedTransitions;
    for(const auto& [srcGlobalId, srcState] : nfa.states) {
        for(const auto& [c, dstIds] : srcState.transitions) {
            for(int dstGlobalId : dstIds) {
                invertedTransitions[dstGlobalId].push_back({srcGlobalId, c});
            }
        }
    }

    for (const auto& [globalId, localId] : globalToLocal) {
        out << "    assign next_state[" << localId << "] = ";
        
        std::vector<std::string> terms;
        if(auto it = invertedTransitions.find(globalId); it != invertedTransitions.end()) {
            for(const auto& [srcGlobalId, c] : it->second) {
                int srcLocal = globalToLocal.at(srcGlobalId);
                terms.push_back("(state_reg[" + std::to_string(srcLocal) + "] && (char_in == 8'd" + std::to_string(c) + "))");
            }
        }

        if (terms.empty()) {
            out << "1'b0;\n";
        } else if (terms.size() == 1) {
            out << terms[0] << ";\n";
        } else {
            out << "(\n";
            for (size_t t = 0; t < terms.size(); ++t) {
                out << "        " << terms[t] << (t < terms.size() - 1 ? " ||\n" : "\n");
            }
            out << "    );\n";
        }
    }

    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            // Reset to start state (one-hot)\n"
        << "            state_reg <= 1 << " << globalToLocal.at(nfa.startStateId) << ";\n"
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
        out << "(|{";
        for (size_t t = 0; t < acceptTerms.size(); ++t) {
            out << acceptTerms[t] << (t < acceptTerms.size() - 1 ? ", " : "");
        }
        out << "})";
    }
    
    out << ";\n"
        << "        end else begin\n"
        << "            match <= 1'b0;\n"
        << "        end\n"
        << "    end\n\n"
        << "endmodule\n";
}

void Emitter::emitTopModule(const std::vector<std::unique_ptr<NFA>>& nfas, const std::filesystem::path& outputDir) {
    auto filePath = outputDir / "top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try {
        out.open(filePath);
    } catch (const std::ios_base::failure& e) {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    out << "module top (\n"
        << "    input  wire        clk,\n"
        << "    input  wire        rst,\n"
        << "    input  wire        start,\n"
        << "    input  wire        end_of_str,\n"
        << "    input  wire [7:0]  char_in,\n"
        << "    output wire [" << (nfas.size() - 1) << ":0] match_bus\n"
        << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i) {
        out << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
            << "        .clk(clk), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[" << i << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

void Emitter::emitTestbench(const std::vector<std::unique_ptr<NFA>>& nfas,
                           const std::filesystem::path& outputDir, 
                           const std::vector<std::string>& testStrings, 
                           const std::vector<std::string>& expectedMatches) {
    auto filePath = outputDir / "tb_top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try {
        out.open(filePath);
    } catch (const std::ios_base::failure& e) {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    size_t numNFAs = nfas.size();

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
        
        for (char c : s) {
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "; #10;\n";
        }
        
        out << "        end_of_str = 1; #10; // Assert for one cycle\n";
        out << "        // Match output is valid on the cycle immediately following end_of_str assertion\n";
        
        if (i < expectedMatches.size()) {
            if (expectedMatches[i].length() != numNFAs) {
                std::string error_msg = "Testbench generation error: expected_matches[" + std::to_string(i) + "] has length " + std::to_string(expectedMatches[i].length()) + " but " + std::to_string(numNFAs) + " NFAs exist.";
                throw std::runtime_error(error_msg);
            }
            out << "        if (match_bus === " << numNFAs << "'b" << expectedMatches[i] << ") begin\n"
                << "            $display(\"PASS: Test case " << i << " ('" << s << "') matches expected mask " << expectedMatches[i] << "\");\n"
                << "        end else begin\n"
                << "            $display(\"FAIL: Test case " << i << " ('" << s << "') expected " << expectedMatches[i] << ", got %b\", match_bus);\n"
                << "        end\n";
        } else {
             out << "        $display(\"INFO: Result for '" << s << "': %b\", match_bus);\n";
        }
        out << "        end_of_str = 0; #10;\n\n"; // De-assert and wait before next test
    }

    out << "        $display(\"All tests completed.\");\n"
        << "        #100; $finish;\n    end\nendmodule\n";
}
