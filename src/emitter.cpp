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

void Emitter::emit(const std::vector<std::unique_ptr<NFA>> &nfas,
                   const std::string &outputDirStr,
                   const std::vector<std::string> &testStrings,
                   const std::vector<std::string> &expectedMatches)
{
    if (nfas.empty())
    {
        std::cout << "No valid NFAs were provided; skipping Verilog emission." << std::endl;
        return;
    }

    std::filesystem::path outputDir(outputDirStr);
    try
    {
        std::filesystem::create_directories(outputDir);
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        throw std::system_error(e.code(), "Failed to create output directory: " + outputDir.string());
    }

    for (const auto &nfa : nfas)
    {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(nfas, outputDir);
    emitUART(outputDir);
    emitUARTTx(outputDir);
    emitTopFPGA(nfas, outputDir);
    emitConstraints(nfas, outputDir);
    emitTestbench(nfas, outputDir, testStrings, expectedMatches);
}

void Emitter::emitNFAModule(const NFA &nfa, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / ("nfa_" + std::to_string(nfa.regexIndex) + ".v");
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    int numStates = static_cast<int>(nfa.states.size());
    if (numStates == 0)
        return;

    std::map<int, int> globalToLocal;
    int localIdx = 0;

    // Ensure start state is always local state 0
    globalToLocal[nfa.startStateId] = localIdx++;

    // Sort other state IDs for deterministic output
    std::set<int> otherIds;
    for (const auto &pair : nfa.states)
    {
        if (pair.first != nfa.startStateId)
            otherIds.insert(pair.first);
    }
    for (int id : otherIds)
    {
        globalToLocal[id] = localIdx++;
    }

    // ADDED: Synchronous enable (en) port instead of gated clock
    out << "`timescale 1ns / 1ps\n\n";
    out << "// NFA for regex index " << nfa.regexIndex << "\n";
    out << "module nfa_" << nfa.regexIndex << " (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       en,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire       end_of_str,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output reg        match\n"
        << ");\n\n";

    out << "    // One-hot state register\n"
        << "    reg [" << numStates - 1 << ":0] state_reg;\n"
        << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    std::map<int, std::vector<std::pair<int, unsigned char>>> invertedTransitions;
    for (const auto &[srcGlobalId, srcState] : nfa.states)
    {
        for (const auto &[c, dstIds] : srcState.transitions)
        {
            for (int dstGlobalId : dstIds)
            {
                invertedTransitions[dstGlobalId].push_back({srcGlobalId, c});
            }
        }
    }

    for (const auto &[globalId, localId] : globalToLocal)
    {
        out << "    assign next_state[" << localId << "] = ";

        std::vector<std::string> terms;

        // FIX: Start state is always active to allow substring matching
        if (localId == 0)
        {
            terms.push_back("1'b1");
        }

        if (auto it = invertedTransitions.find(globalId); it != invertedTransitions.end())
        {
            std::map<int, std::vector<unsigned char>> charsBySrc;
            for (const auto &[srcGlobalId, c] : it->second)
            {
                charsBySrc[globalToLocal.at(srcGlobalId)].push_back(c);
            }

            // Magnitude comparators for contiguous wildcard ranges
            for (auto &[srcLocal, chars] : charsBySrc)
            {
                std::sort(chars.begin(), chars.end());
                size_t i = 0;
                while (i < chars.size())
                {
                    size_t j = i;
                    while (j + 1 < chars.size() && chars[j + 1] == chars[j] + 1)
                    {
                        j++;
                    }
                    if (j - i >= 3)
                    {
                        terms.push_back("(state_reg[" + std::to_string(srcLocal) +
                                        "] && (char_in >= 8'd" + std::to_string(chars[i]) +
                                        ") && (char_in <= 8'd" + std::to_string(chars[j]) + "))");
                    }
                    else
                    {
                        for (size_t k = i; k <= j; ++k)
                        {
                            terms.push_back("(state_reg[" + std::to_string(srcLocal) +
                                            "] && (char_in == 8'd" + std::to_string(chars[k]) + "))");
                        }
                    }
                    i = j + 1;
                }
            }
        }

        if (terms.empty())
        {
            out << "1'b0;\n";
        }
        else
        {
            out << terms[0];
            for (size_t t = 1; t < terms.size(); ++t)
            {
                out << " | " << terms[t];
            }
            out << ";\n";
        }
    }

    // FIX: Using synchronous enable
    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            // Reset to start state (one-hot)\n"
        << "            state_reg <= 1 << " << globalToLocal.at(nfa.startStateId) << ";\n"
        << "        end else if (en) begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n"
        << "    // Match logic: asserted immediately on accept state\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (en) begin\n"
        << "            match <= ";

    std::vector<std::string> acceptTerms;
    for (const auto &[id, state] : nfa.states)
    {
        if (state.isAccept)
        {
            acceptTerms.push_back("state_reg[" + std::to_string(globalToLocal.at(id)) + "]");
        }
    }

    if (acceptTerms.empty())
    {
        out << "1'b0";
    }
    else if (acceptTerms.size() == 1)
    {
        out << acceptTerms[0];
    }
    else
    {
        out << "(|{";
        for (size_t t = 0; t < acceptTerms.size(); ++t)
        {
            out << acceptTerms[t] << (t < acceptTerms.size() - 1 ? ", " : "");
        }
        out << "})";
    }

    out << ";\n"
        << "        end\n"
        << "    end\n\n"
        << "endmodule\n";
}

void Emitter::emitTopModule(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    out << "`timescale 1ns / 1ps\n\n";
    out << "module top (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       en,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire       end_of_str,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output wire [" << (nfas.size() - 1) << ":0] match_bus\n"
        << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i)
    {
        out << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
            << "        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[" << i << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

void Emitter::emitTestbench(const std::vector<std::unique_ptr<NFA>> &nfas,
                            const std::filesystem::path &outputDir,
                            const std::vector<std::string> &testStrings,
                            const std::vector<std::string> &expectedMatches)
{
    auto filePath = outputDir / "tb_top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    size_t numNFAs = nfas.size();

    out << "`timescale 1ns / 1ps\n\n"
        << "module tb_top;\n"
        << "    reg clk, en, rst, start, end_of_str;\n"
        << "    reg [7:0] char_in;\n"
        << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
        << "    top uut (.clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match_bus(match_bus));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    initial begin\n"
        << "        // synthesis translate_off\n"
        << "        `ifndef SYNTHESIS\n"
        << "        $dumpfile(\"dump.vcd\");\n"
        << "        $dumpvars(0, tb_top);\n"
        << "        `endif\n"
        << "        // synthesis translate_on\n\n"
        << "        clk = 0; en = 1; rst = 1; start = 0; end_of_str = 0; char_in = 0;\n"
        << "        #20 rst = 0; #10;\n\n";

    for (size_t i = 0; i < testStrings.size(); ++i)
    {
        const std::string &s = testStrings[i];
        out << "        // Test case " << i << ": \"" << s << "\"\n"
            << "        start = 1; #10 start = 0;\n";

        for (char c : s)
        {
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "; #10;\n";
        }

        out << "        end_of_str = 1; #10; // Assert for one cycle\n";
        out << "        // Match output is valid on the cycle immediately following end_of_str assertion\n";

        if (i < expectedMatches.size())
        {
            if (expectedMatches[i].length() != numNFAs)
            {
                std::string error_msg = "Testbench generation error: expected_matches[" + std::to_string(i) + "] has length " + std::to_string(expectedMatches[i].length()) + " but " + std::to_string(numNFAs) + " NFAs exist.";
                throw std::runtime_error(error_msg);
            }
            out << "        if (match_bus === " << numNFAs << "'b" << expectedMatches[i] << ") begin\n"
                << "            $display(\"PASS: Test case " << i << " ('" << s << "') matches expected mask " << expectedMatches[i] << "\");\n"
                << "        end else begin\n"
                << "            $display(\"FAIL: Test case " << i << " ('" << s << "') expected " << expectedMatches[i] << ", got %b\", match_bus);\n"
                << "        end\n";
        }
        else
        {
            out << "        $display(\"INFO: Result for '" << s << "': %b\", match_bus);\n";
        }
        out << "        end_of_str = 0; #10;\n\n";
    }

    out << "        $display(\"All tests completed.\");\n"
        << "        #100; $finish;\n    end\nendmodule\n";
}

void Emitter::emitUART(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_rx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_rx #(
    parameter CLKS_PER_BIT = 868 // 100 MHz / 115200 Baud
)(
    input  wire       clk,
    input  wire       rx,
    output reg  [7:0] rx_data,
    output reg        rx_ready
);
    localparam IDLE = 2'b00, START_BIT = 2'b01, DATA_BITS = 2'b10, STOP_BIT = 2'b11;
    reg [1:0] state = IDLE;
    reg [9:0] clk_count = 0;
    reg [2:0] bit_idx = 0;

    always @(posedge clk) begin
        case (state)
            IDLE: begin
                rx_ready <= 1'b0;
                clk_count <= 0;
                bit_idx <= 0;
                if (rx == 1'b0) state <= START_BIT;
            end
            START_BIT: begin
                if (clk_count == (CLKS_PER_BIT-1)/2) begin
                    if (rx == 1'b0) begin
                        clk_count <= 0;
                        state <= DATA_BITS;
                    end else state <= IDLE;
                end else clk_count <= clk_count + 1;
            end
            DATA_BITS: begin
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    rx_data[bit_idx] <= rx;
                    if (bit_idx < 7) bit_idx <= bit_idx + 1;
                    else state <= STOP_BIT;
                end
            end
            STOP_BIT: begin
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    // FIX: Check for framing error
                    if (rx == 1'b1) rx_ready <= 1'b1; 
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}

void Emitter::emitUARTTx(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_tx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_tx #(
    parameter CLKS_PER_BIT = 868 // 100 MHz / 115200 Baud
)(
    input  wire       clk,
    input  wire       tx_start,
    input  wire [7:0] tx_data,
    output reg        tx,
    output reg        tx_busy
);
    localparam IDLE = 2'b00, START_BIT = 2'b01, DATA_BITS = 2'b10, STOP_BIT = 2'b11;
    reg [1:0] state = IDLE;
    reg [9:0] clk_count = 0;
    reg [2:0] bit_idx = 0;
    reg [7:0] tx_data_latch = 0;

    initial begin
        tx = 1'b1;
        tx_busy = 1'b0;
    end

    always @(posedge clk) begin
        case (state)
            IDLE: begin
                tx <= 1'b1;
                tx_busy <= 1'b0;
                if (tx_start) begin
                    tx_data_latch <= tx_data;
                    tx_busy <= 1'b1;
                    state <= START_BIT;
                    clk_count <= 0;
                end
            end
            START_BIT: begin
                tx <= 1'b0;
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    bit_idx <= 0;
                    state <= DATA_BITS;
                end
            end
            DATA_BITS: begin
                tx <= tx_data_latch[bit_idx];
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    if (bit_idx < 7) bit_idx <= bit_idx + 1;
                    else state <= STOP_BIT;
                end
            end
            STOP_BIT: begin
                tx <= 1'b1;
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}


void Emitter::emitTopFPGA(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top_fpga.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    size_t numNFAs = nfas.size();

    out << "`timescale 1ns / 1ps\n\n";
    out << "module top_fpga (\n"
        << "    input  wire clk,        // 100 MHz system clock\n"
        << "    input  wire rst_btn,    // Physical reset button\n"
        << "    input  wire uart_rx_pin,// USB UART RX pin\n"
        << "    output wire uart_tx_pin,// USB UART TX pin\n"
        << "    output reg  [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_leds // LEDs for match output\n"
        << ");\n\n";

    out << "    function [4:0] get_redaction_length;\n"
        << "        input [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] matches;\n"
        << "        begin\n"
        << "            get_redaction_length = 5'd0;\n";
    for (size_t i = 0; i < numNFAs; ++i)
    {
        // Heuristic: number of states - 1, capped at 31
        int len = nfas[i]->states.size() - 1;
        if (len > 31)
            len = 31;
        out << "            if (matches[" << i << "]) get_redaction_length = 5'd" << len << ";\n";
    }
    out << "        end\n"
        << "    endfunction\n\n";

    out << R"(
    wire [7:0] rx_data;
    wire       rx_ready;
    
    uart_rx uart_inst (
        .clk(clk),
        .rx(uart_rx_pin),
        .rx_data(rx_data),
        .rx_ready(rx_ready)
    );

    reg        nfa_start = 1'b1;
    reg        nfa_end_of_str = 1'b0;
    reg  [7:0] nfa_char_in = 8'h00;
    reg        nfa_en = 1'b0;
)";

    out << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n";

    out << "    top regex_engine (\n"
        << "        .clk(clk),\n"
        << "        .en(nfa_en),\n"
        << "        .rst(rst_btn),\n"
        << "        .start(nfa_start),\n"
        << "        .end_of_str(nfa_end_of_str),\n"
        << "        .char_in(nfa_char_in),\n"
        << "        .match_bus(match_bus)\n"
        << "    );\n\n";

    out << R"(
    // UART TX instantiation
    reg        tx_start = 1'b0;
    reg  [7:0] tx_data = 8'h00;
    wire       tx_busy;

    uart_tx uart_tx_inst (
        .clk(clk),
        .tx_start(tx_start),
        .tx_data(tx_data),
        .tx(uart_tx_pin),
        .tx_busy(tx_busy)
    );

    // 32-Byte Delay Line
    reg [7:0] delay_line [0:31];
    integer i;
    
    reg [4:0] redact_counter = 0;
    wire [7:0] mux_out = (redact_counter > 0) ? 8'h58 : delay_line[31]; // 'X' is 8'h58

    reg rx_ready_prev = 0;
    
    always @(posedge clk) begin
        if (rst_btn) begin
            nfa_en <= 0;
            nfa_start <= 1; // Reset NFA
            redact_counter <= 0;
            tx_start <= 0;
            rx_ready_prev <= 0;
        end else begin
            nfa_start <= 0; // Release NFA reset
            nfa_en <= 0;
            tx_start <= 0;
            rx_ready_prev <= rx_ready;

            // Handle Redaction Counter Decay
            if (redact_counter > 0 && rx_ready && !rx_ready_prev) begin
                redact_counter <= redact_counter - 1;
            end

            // Match Event: Overrides decay to extend the window
            if (|match_bus) begin
                redact_counter <= get_redaction_length(match_bus);
                match_leds <= match_bus; // Visual feedback
            end

            // Process Incoming Byte
            if (rx_ready && !rx_ready_prev) begin
                // Shift delay line
                for (i=31; i>0; i=i-1) delay_line[i] <= delay_line[i-1];
                delay_line[0] <= rx_data;
                
                // Feed NFA
                nfa_char_in <= rx_data;
                nfa_en <= 1; // Pulse NFA enable
                
                // Transmit the delayed byte out
                tx_data <= mux_out;
                tx_start <= 1;
            end
        end
    end
endmodule
)";
}

void Emitter::emitConstraints(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "constraints.xdc";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "## Clock signal (Nexys A7 100MHz)\n"
        << "set_property PACKAGE_PIN E3 [get_ports clk]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports clk]\n"
        << "create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports clk]\n\n";

    out << "## USB-RS232 Interface (Nexys A7)\n"
        << "set_property PACKAGE_PIN C4 [get_ports uart_rx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_pin]\n"
        << "set_property PACKAGE_PIN D4 [get_ports uart_tx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_pin]\n\n";

    out << "## Buttons (Nexys A7 Center Button - BTNC)\n"
        << "set_property PACKAGE_PIN N17 [get_ports rst_btn]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports rst_btn]\n\n";

    out << "## LEDs (Nexys A7 LED0 to LED15)\n";
    std::vector<std::string> led_pins = {
        "H17", "K15", "J13", "N14", "R18", "V17", "U17", "U16",
        "V16", "T15", "U14", "T16", "V15", "V14", "V12", "V11"};
    for (size_t i = 0; i < nfas.size() && i < led_pins.size(); ++i)
    {
        out << "set_property PACKAGE_PIN " << led_pins[i] << " [get_ports {match_leds[" << i << "]}]\n"
            << "set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[" << i << "]}]\n";
    }
}