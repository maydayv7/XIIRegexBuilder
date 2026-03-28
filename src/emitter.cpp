#include "emitter.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <iomanip>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

Emitter::Emitter(const std::vector<std::unique_ptr<NFA>>& nfas) : nfas(nfas) {}

void Emitter::addTestCase(const std::string& input, const std::vector<bool>& matches) {
    testCases.push_back({input, matches});
}

void Emitter::emit(const std::string& outputDir) {
    try {
        if (!fs::exists(outputDir)) {
            fs::create_directories(outputDir);
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating output directory: " << e.what() << std::endl;
        throw;
    }

    for (const auto& nfa : nfas) {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(outputDir);
    emitTestbench(outputDir);
}

void Emitter::emitNFAModule(const NFA& nfa, const std::string& outputDir) {
    std::string filename = outputDir + "/nfa_" + std::to_string(nfa.regexIndex) + ".v";
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing." << std::endl;
        throw std::runtime_error("Could not write NFA module file");
    }

    int numStates = nfa.states.size();
    
    std::map<int, int> globalToLocal;
    int idx = 0;
    globalToLocal[nfa.startStateId] = idx++;
    for (const auto& [id, state] : nfa.states) {
        if (id != nfa.startStateId) {
            globalToLocal[id] = idx++;
        }
    }

    f << "module nfa_" << nfa.regexIndex << " (\n"
      << "    input wire clk,\n"
      << "    input wire rst,\n"
      << "    input wire start,\n"
      << "    input wire end_of_str,\n"
      << "    input wire [7:0] char_in,\n"
      << "    output reg match\n"
      << ");\n\n";

    f << "    // One-hot state register\n"
      << "    reg [" << numStates - 1 << ":0] state_reg;\n"
      << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    for (int j = 0; j < numStates; ++j) {
        f << "    assign next_state[" << j << "] = ";
        
        std::vector<std::string> terms;
        for (const auto& [srcId, srcState] : nfa.states) {
            int srcLocal = globalToLocal.at(srcId);
            for (const auto& [c, dstIds] : srcState.transitions) {
                for (int dstId : dstIds) {
                    if (globalToLocal.at(dstId) == j) {
                        terms.push_back("(state_reg[" + std::to_string(srcLocal) + "] && (char_in == 8'd" + std::to_string((int)c) + "))");
                    }
                }
            }
        }

        if (terms.empty()) {
            f << "1'b0;\n";
        } else if (terms.size() == 1) {
            f << terms[0] << ";\n";
        } else {
            f << "(\n";
            for (size_t t = 0; t < terms.size(); ++t) {
                f << "        " << terms[t];
                if (t < terms.size() - 1) f << " ||\n";
            }
            f << "\n    );\n";
        }
    }

    f << "\n    always @(posedge clk) begin\n"
      << "        if (rst) begin\n"
      << "            state_reg <= {" << (numStates > 1 ? "{" + std::to_string(numStates - 1) + "{1'b0}}, " : "") << "1'b1};\n"
      << "        end else if (start) begin\n"
      << "            state_reg <= {" << (numStates > 1 ? "{" + std::to_string(numStates - 1) + "{1'b0}}, " : "") << "1'b1};\n"
      << "        end else begin\n"
      << "            state_reg <= next_state;\n"
      << "        end\n"
      << "    end\n\n";

    f << "    always @(posedge clk) begin\n"
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
        f << "1'b0";
    } else if (acceptTerms.size() == 1) {
        f << acceptTerms[0];
    } else {
        f << "(\n";
        for (size_t t = 0; t < acceptTerms.size(); ++t) {
            f << "                " << acceptTerms[t];
            if (t < acceptTerms.size() - 1) f << " ||\n";
        }
        f << "\n            )";
    }
    
    f << ";\n"
      << "        end else begin\n"
      << "            match <= 1'b0;\n"
      << "        end\n"
      << "    end\n\n"
      << "endmodule\n";
}

void Emitter::emitTopModule(const std::string& outputDir) {
    std::string filename = outputDir + "/top.v";
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing." << std::endl;
        throw std::runtime_error("Could not write top module file");
    }

    int numNFAs = nfas.size();

    f << "module top (\n"
      << "    input wire clk,\n"
      << "    input wire rst,\n"
      << "    input wire start,\n"
      << "    input wire end_of_str,\n"
      << "    input wire [7:0] char_in,\n"
      << "    output wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus\n"
      << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i) {
        f << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
          << "        .clk(clk),\n"
          << "        .rst(rst),\n"
          << "        .start(start),\n"
          << "        .end_of_str(end_of_str),\n"
          << "        .char_in(char_in),\n"
          << "        .match(match_bus[" << i << "])\n"
          << "    );\n\n";
    }

    f << "endmodule\n";
}

void Emitter::emitTestbench(const std::string& outputDir) {
    std::string filename = outputDir + "/tb_top.v";
    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open " << filename << " for writing." << std::endl;
        throw std::runtime_error("Could not write testbench file");
    }

    int numNFAs = nfas.size();

    f << "`timescale 1ns / 1ps\n\n"
      << "module tb_top();\n\n"
      << "    reg clk;\n"
      << "    reg rst;\n"
      << "    reg start;\n"
      << "    reg end_of_str;\n"
      << "    reg [7:0] char_in;\n"
      << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
      << "    top uut (\n"
      << "        .clk(clk),\n"
      << "        .rst(rst),\n"
      << "        .start(start),\n"
      << "        .end_of_str(end_of_str),\n"
      << "        .char_in(char_in),\n"
      << "        .match_bus(match_bus)\n"
      << "    );\n\n"
      << "    always #5 clk = ~clk;\n\n"
      << "    task run_test(input string name, input string test_str, input [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] expected);\n"
      << "        integer i;\n"
      << "        begin\n"
      << "            $display(\"Testing [%s]: \\\"%s\\\"\", name, test_str);\n"
      << "            start = 1;\n"
      << "            @(posedge clk);\n"
      << "            start = 0;\n"
      << "            for (i = 0; i < test_str.len(); i = i + 1) begin\n"
      << "                char_in = test_str.getc(i);\n"
      << "                @(posedge clk);\n"
      << "            end\n"
      << "            end_of_str = 1;\n"
      << "            @(posedge clk);\n"
      << "            end_of_str = 0;\n"
      << "            @(posedge clk);\n" // Registered output delay
      << "            if (match_bus === expected) begin\n"
      << "                $display(\"  PASS: match_bus = %b\", match_bus);\n"
      << "            end else begin\n"
      << "                $display(\"  FAIL: match_bus = %b, expected = %b\", match_bus, expected);\n"
      << "            end\n"
      << "            #20;\n"
      << "        end\n"
      << "    endtask\n\n"
      << "    initial begin\n"
      << "        // synthesis translate_off\n"
      << "        `ifndef SYNTHESIS\n"
      << "        $dumpfile(\"dump.vcd\");\n"
      << "        $dumpvars(0, tb_top);\n"
      << "        `endif\n"
      << "        // synthesis translate_on\n\n"
      << "        clk = 0;\n"
      << "        rst = 1;\n"
      << "        start = 0;\n"
      << "        end_of_str = 0;\n"
      << "        char_in = 0;\n\n"
      << "        #20 rst = 0;\n"
      << "        #20;\n\n";

    for (size_t i = 0; i < testCases.size(); ++i) {
        f << "        run_test(\"Test " << i << "\", \"" << testCases[i].input << "\", " << numNFAs << "'b";
        for (int j = numNFAs - 1; j >= 0; --j) {
            f << (testCases[i].expectedMatches[j] ? "1" : "0");
        }
        f << ");\n";
    }

    f << "\n        $display(\"All tests completed.\");\n"
      << "        #100;\n"
      << "        $finish;\n"
      << "    end\n\n"
      << "endmodule\n";
}
