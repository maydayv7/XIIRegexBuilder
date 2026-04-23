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

// =============================================================================
// Emitter::emit — orchestration
// =============================================================================
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
    emitUARTRX(outputDir);        // uart_rx.v
    emitUARTTX(outputDir);        // uart_tx.v
    emitFIFO(outputDir);          // uart_rx_fifo.v
    
    // New Ethernet modules
    emitPacketParserFSM(outputDir, static_cast<int>(nfas.size()));
    emitResultAssembler(outputDir, static_cast<int>(nfas.size()));

    emitTopFPGA(nfas, outputDir); // top_fpga.v
    emitConstraints(nfas, outputDir);
    emitTestbench(nfas, outputDir, testStrings, expectedMatches);
}

// =============================================================================
// emitNFAModule — one file per regex
// =============================================================================
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

    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            // Reset to start state (one-hot)\n"
        << "            state_reg <= 1 << " << globalToLocal.at(nfa.startStateId) << ";\n"
        << "        end else if (en) begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n"
        << "    // Match logic: asserted on cycle following end_of_str\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (en) begin\n"
        << "            if (end_of_str) begin\n"
        << "                match <= ";

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
        << "            end else begin\n"
        << "                match <= 1'b0;\n"
        << "            end\n"
        << "        end\n"
        << "    end\n\n"
        << "endmodule\n";
}

// =============================================================================
// emitTopModule — top.v wrapper
// =============================================================================
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

// =============================================================================
// emitTestbench — tb_top.v
// =============================================================================
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
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "; #10;\n";
        }

        out << "        end_of_str = 1; #10;\n";
        out << "        // Match output is valid on the cycle immediately following end_of_str assertion\n";

        if (i < expectedMatches.size())
        {
            if (expectedMatches[i].length() != numNFAs)
            {
                throw std::runtime_error(
                    "Testbench generation error: expected_matches[" + std::to_string(i) +
                    "] has length " + std::to_string(expectedMatches[i].length()) +
                    " but " + std::to_string(numNFAs) + " NFAs exist.");
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

// =============================================================================
// emitUARTRX — uart_rx.v
// =============================================================================
void Emitter::emitUARTRX(const std::filesystem::path &outputDir)
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
                    if (rx == 1'b1) rx_ready <= 1'b1;
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}

// =============================================================================
// emitUARTTX - uart_tx.v
// =============================================================================
void Emitter::emitUARTTX(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_tx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_tx #(
    parameter CLKS_PER_BIT = 868   // 100 MHz / 115200 baud
)(
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] tx_data,
    input  wire       tx_start,
    output reg        tx_busy,
    output reg        tx
);
    localparam S_IDLE      = 3'd0;
    localparam S_START_BIT = 3'd1;
    localparam S_DATA_BITS = 3'd2;
    localparam S_STOP_BIT  = 3'd3;

    reg [2:0]  state    = S_IDLE;
    reg [9:0]  clk_cnt  = 10'd0;
    reg [2:0]  bit_idx  = 3'd0;
    reg [7:0]  tx_shift = 8'd0;

    always @(posedge clk) begin
        if (rst) begin
            state    <= S_IDLE;
            tx       <= 1'b1;
            tx_busy  <= 1'b0;
            clk_cnt  <= 10'd0;
            bit_idx  <= 3'd0;
            tx_shift <= 8'd0;
        end else begin
            case (state)
                S_IDLE: begin
                    tx      <= 1'b1;
                    tx_busy <= 1'b0;
                    clk_cnt <= 10'd0;
                    bit_idx <= 3'd0;
                    if (tx_start) begin
                        tx_shift <= tx_data;
                        tx_busy  <= 1'b1;
                        state    <= S_START_BIT;
                    end
                end
                S_START_BIT: begin
                    tx <= 1'b0;
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt <= 10'd0;
                        state   <= S_DATA_BITS;
                    end
                end
                S_DATA_BITS: begin
                    tx <= tx_shift[0];
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt  <= 10'd0;
                        tx_shift <= tx_shift >> 1;
                        if (bit_idx < 7)
                            bit_idx <= bit_idx + 1;
                        else begin
                            bit_idx <= 3'd0;
                            state   <= S_STOP_BIT;
                        end
                    end
                end
                S_STOP_BIT: begin
                    tx <= 1'b1;
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt <= 10'd0;
                        state   <= S_IDLE;
                    end
                end
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
)";
}

// =============================================================================
// emitFIFO - uart_rx_fifo.v
// =============================================================================
void Emitter::emitFIFO(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_rx_fifo.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_rx_fifo #(
    parameter DEPTH_LOG2 = 4        // depth = 2^DEPTH_LOG2 = 16
)(
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] wr_data,
    input  wire       wr_en,
    output wire       full,
    output wire [7:0] rd_data,
    input  wire       rd_en,
    output wire       empty
);
    localparam DEPTH = 1 << DEPTH_LOG2;

    reg [7:0] mem [0:DEPTH-1];
    reg [DEPTH_LOG2-1:0] wr_ptr = {DEPTH_LOG2{1'b0}};
    reg [DEPTH_LOG2-1:0] rd_ptr = {DEPTH_LOG2{1'b0}};
    reg [DEPTH_LOG2  :0] count  = {(DEPTH_LOG2+1){1'b0}};

    assign full    = (count == DEPTH[DEPTH_LOG2:0]);
    assign empty   = (count == {(DEPTH_LOG2+1){1'b0}});
    assign rd_data = mem[rd_ptr];

    always @(posedge clk) begin
        if (rst) begin
            wr_ptr <= {DEPTH_LOG2{1'b0}};
            rd_ptr <= {DEPTH_LOG2{1'b0}};
            count  <= {(DEPTH_LOG2+1){1'b0}};
        end else begin
            if (wr_en && !full && rd_en && !empty) begin
                mem[wr_ptr] <= wr_data;
                wr_ptr      <= wr_ptr + 1;
                rd_ptr      <= rd_ptr + 1;
            end else if (wr_en && !full) begin
                mem[wr_ptr] <= wr_data;
                wr_ptr      <= wr_ptr + 1;
                count       <= count + 1;
            end else if (rd_en && !empty) begin
                rd_ptr <= rd_ptr + 1;
                count  <= count - 1;
            end
        end
    end
endmodule
)";
}

// =============================================================================
// emitPacketParserFSM - packet_parser_fsm.v
// =============================================================================
void Emitter::emitPacketParserFSM(const std::filesystem::path &outputDir, int numRegex)
{
    auto filePath = outputDir / "packet_parser_fsm.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << "module packet_parser_fsm #(\n"
        << "    parameter NUM_REGEX = " << numRegex << "\n"
        << ")(\n"
        << "    input  wire        clk,\n"
        << "    input  wire        rst,\n\n"
        << "    // UDP payload input (AXI-Stream from udp_rx)\n"
        << "    input  wire [7:0]  udp_payload_tdata,\n"
        << "    input  wire        udp_payload_tvalid,\n"
        << "    input  wire        udp_payload_tlast,\n"
        << "    output reg         udp_payload_tready,\n\n"
        << "    // NFA engine interface\n"
        << "    output reg         nfa_en,\n"
        << "    output reg         nfa_rst,\n"
        << "    output reg         nfa_start,\n"
        << "    output reg         nfa_end_of_str,\n"
        << "    output reg  [7:0]  nfa_char_in,\n\n"
        << "    // Result capture interface\n"
        << "    input  wire [NUM_REGEX-1:0]        match_bus,\n"
        << "    output reg                         result_valid,\n"
        << "    output reg  [NUM_REGEX-1:0]        result_match,\n"
        << "    output reg  [31:0]                 result_seq_num,\n"
        << "    output reg  [15:0]                 result_num_strings\n"
        << ");\n\n";

    out << R"(
    localparam IDLE            = 4'd0;
    localparam READ_SEQ_0      = 4'd1;
    localparam READ_SEQ_1      = 4'd2;
    localparam READ_SEQ_2      = 4'd3;
    localparam READ_SEQ_3      = 4'd4;
    localparam READ_NUM_STR_HI = 4'd5;
    localparam READ_NUM_STR_LO = 4'd6;
    localparam READ_STR_LEN    = 4'd7;
    localparam STREAM_CHAR     = 4'd8;
    localparam WAIT_NFA_DONE   = 4'd9;
    localparam WAIT_MATCH      = 4'd10;
    localparam STORE_RESULT    = 4'd11;
    localparam NEXT_OR_DONE    = 4'd12;
    localparam TRIGGER_TX      = 4'd13;
    localparam RESET_NFA       = 4'd14;

    reg [3:0]  state = IDLE;
    reg [31:0] seq_num;
    reg [15:0] num_strings;
    reg [15:0] str_idx;
    reg [7:0]  cur_str_len;
    reg [7:0]  char_idx;

    always @(posedge clk) begin
        if (rst) begin
            state <= IDLE;
            udp_payload_tready <= 1'b0;
            nfa_en <= 1'b0;
            nfa_rst <= 1'b1;
            nfa_start <= 1'b0;
            nfa_end_of_str <= 1'b0;
            result_valid <= 1'b0;
        end else begin
            udp_payload_tready <= 1'b0;
            result_valid <= 1'b0;
            nfa_rst <= 1'b0;
            nfa_start <= 1'b0;
            nfa_end_of_str <= 1'b0;
            nfa_en <= 1'b0;

            case (state)
                IDLE: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        seq_num[31:24] <= udp_payload_tdata;
                        state <= READ_SEQ_1;
                    end
                end
                READ_SEQ_1: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        seq_num[23:16] <= udp_payload_tdata;
                        state <= READ_SEQ_2;
                    end
                end
                READ_SEQ_2: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        seq_num[15:8] <= udp_payload_tdata;
                        state <= READ_SEQ_3;
                    end
                end
                READ_SEQ_3: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        seq_num[7:0] <= udp_payload_tdata;
                        state <= READ_NUM_STR_HI;
                    end
                end
                READ_NUM_STR_HI: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        num_strings[15:8] <= udp_payload_tdata;
                        state <= READ_NUM_STR_LO;
                    end
                end
                READ_NUM_STR_LO: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        num_strings[7:0] <= udp_payload_tdata;
                        str_idx <= 0;
                        result_seq_num <= seq_num;
                        result_num_strings <= {num_strings[15:8], udp_payload_tdata};
                        state <= RESET_NFA;
                    end
                end
                RESET_NFA: begin
                    nfa_rst <= 1'b1; // Hold reset for 1 cycle
                    state <= READ_STR_LEN;
                end
                READ_STR_LEN: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        cur_str_len <= udp_payload_tdata;
                        char_idx <= 0;
                        if (udp_payload_tdata == 0) state <= WAIT_NFA_DONE;
                        else state <= STREAM_CHAR;
                    end
                end
                STREAM_CHAR: begin
                    udp_payload_tready <= 1'b1;
                    if (udp_payload_tvalid) begin
                        nfa_char_in <= udp_payload_tdata;
                        nfa_en <= 1'b1;
                        if (char_idx + 1 == cur_str_len) state <= WAIT_NFA_DONE;
                        else char_idx <= char_idx + 1;
                    end
                end
                WAIT_NFA_DONE: begin
                    nfa_end_of_str <= 1'b1;
                    nfa_en <= 1'b1;
                    state <= WAIT_MATCH;
                end
                WAIT_MATCH: begin
                    // Extra cycle for NFA to register final match bits
                    nfa_en <= 1'b1; 
                    state <= STORE_RESULT;
                end
                STORE_RESULT: begin
                    result_valid <= 1'b1;
                    result_match <= match_bus;
                    str_idx <= str_idx + 1;
                    state <= NEXT_OR_DONE;
                end
                NEXT_OR_DONE: begin
                    if (str_idx < num_strings) begin
                        state <= RESET_NFA;
                    end else begin
                        state <= TRIGGER_TX;
                    end
                end
                TRIGGER_TX: begin
                    // This state can be used to signal the result_assembler to fire the packet
                    state <= IDLE;
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule
)";
}

// =============================================================================
// emitResultAssembler - result_assembler.v
// =============================================================================
void Emitter::emitResultAssembler(const std::filesystem::path &outputDir, int numRegex)
{
    auto filePath = outputDir / "result_assembler.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    int matchBitsBytes = (numRegex + 7) / 8;
    int resultSizeBytes = matchBitsBytes + (numRegex * 2);

    out << "`timescale 1ns / 1ps\n\n";
    out << "module result_assembler #(\n"
        << "    parameter NUM_REGEX = " << numRegex << "\n"
        << ")(\n"
        << "    input  wire        clk,\n"
        << "    input  wire        rst,\n\n"
        << "    // Input from parser FSM\n"
        << "    input  wire                         result_valid,\n"
        << "    input  wire [NUM_REGEX-1:0]        result_match,\n"
        << "    input  wire [31:0]                 result_seq_num,\n"
        << "    input  wire [15:0]                 result_num_strings,\n"
        << "    input  wire [31:0]                 cycle_stamp,\n\n"
        << "    // Match counts from top_fpga\n";
    
    for (int i = 0; i < numRegex; ++i) {
        out << "    input  wire [15:0]                 match_count_" << i << ",\n";
    }

    out << "    // AXI-Stream output to udp_tx\n"
        << "    output reg  [7:0]  m_axis_tdata,\n"
        << "    output reg         m_axis_tvalid,\n"
        << "    output reg         m_axis_tlast,\n"
        << "    input  wire        m_axis_tready\n"
        << ");\n\n";

    out << "    localparam RESULT_SIZE = " << resultSizeBytes << ";\n"
        << "    localparam MAX_RESULTS = 16;\n\n"
        << "    reg [7:0] mem [0:8191]; // Buffer for results (8KB)\n"
        << "    reg [12:0] write_ptr = 0;\n"
        << "    reg [12:0] read_ptr = 0;\n"
        << "    reg [15:0] results_count = 0;\n"
        << "    reg [31:0] active_seq_num;\n"
        << "    reg [15:0] active_num_strings;\n"
        << "    reg [31:0] active_cycle_stamp;\n\n"
        << "    localparam IDLE = 2'd0, SEND_HEADER = 2'd1, SEND_RESULTS = 2'd2;\n"
        << "    reg [1:0] state = IDLE;\n"
        << "    reg [10:0] send_count = 0;\n"
        << "    reg [3:0] header_idx = 0;\n\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst) begin\n"
        << "            state <= IDLE;\n"
        << "            write_ptr <= 0;\n"
        << "            results_count <= 0;\n"
        << "            m_axis_tvalid <= 0;\n"
        << "        end else begin\n"
        << "            if (result_valid) begin\n"
        << "                if (results_count == 0) begin\n"
        << "                    active_seq_num <= result_seq_num;\n"
        << "                    active_num_strings <= result_num_strings;\n"
        << "                    active_cycle_stamp <= cycle_stamp;\n"
        << "                    write_ptr <= 0;\n"
        << "                end\n";

    // Write match bits
    for (int b = 0; b < matchBitsBytes; ++b) {
        int startBit = b * 8;
        int endBit = std::min((b + 1) * 8 - 1, numRegex - 1);
        out << "                mem[write_ptr + " << b << "] <= {";
        if (endBit - startBit + 1 < 8) {
            out << (8 - (endBit - startBit + 1)) << "'b0, ";
        }
        for (int i = endBit; i >= startBit; --i) {
            out << "result_match[" << i << "]" << (i > startBit ? ", " : "");
        }
        out << "};\n";
    }

    // Write hit counts
    for (int i = 0; i < numRegex; ++i) {
        out << "                mem[write_ptr + " << matchBitsBytes + (i * 2) << "] <= match_count_" << i << "[15:8];\n"
            << "                mem[write_ptr + " << matchBitsBytes + (i * 2) + 1 << "] <= match_count_" << i << "[7:0];\n";
    }

    out << "                write_ptr <= write_ptr + RESULT_SIZE;\n"
        << "                results_count <= results_count + 1;\n"
        << "            end\n\n"
        << "            case (state)\n"
        << "                IDLE: begin\n"
        << "                    if (results_count > 0 && results_count == active_num_strings) begin\n"
        << "                        state <= SEND_HEADER;\n"
        << "                        header_idx <= 0;\n"
        << "                        read_ptr <= 0;\n"
        << "                    end\n"
        << "                end\n"
        << "                SEND_HEADER: begin\n"
        << "                    m_axis_tvalid <= 1;\n"
        << "                    case (header_idx)\n"
        << "                        0: m_axis_tdata <= active_seq_num[31:24];\n"
        << "                        1: m_axis_tdata <= active_seq_num[23:16];\n"
        << "                        2: m_axis_tdata <= active_seq_num[15:8];\n"
        << "                        3: m_axis_tdata <= active_seq_num[7:0];\n"
        << "                        4: m_axis_tdata <= active_num_strings[15:8];\n"
        << "                        5: m_axis_tdata <= active_num_strings[7:0];\n"
        << "                        6: m_axis_tdata <= active_cycle_stamp[31:24];\n"
        << "                        7: m_axis_tdata <= active_cycle_stamp[23:16];\n"
        << "                        8: m_axis_tdata <= active_cycle_stamp[15:8];\n"
        << "                        9: m_axis_tdata <= active_cycle_stamp[7:0];\n"
        << "                    endcase\n"
        << "                    if (m_axis_tready) begin\n"
        << "                        if (header_idx == 9) begin\n"
        << "                            state <= SEND_RESULTS;\n"
        << "                            send_count <= 0;\n"
        << "                        end else header_idx <= header_idx + 1;\n"
        << "                    end\n"
        << "                end\n"
        << "                SEND_RESULTS: begin\n"
        << "                    m_axis_tvalid <= 1;\n"
        << "                    m_axis_tdata <= mem[read_ptr];\n"
        << "                    m_axis_tlast <= (read_ptr + 1 == write_ptr);\n"
        << "                    if (m_axis_tready) begin\n"
        << "                        if (read_ptr + 1 == write_ptr) begin\n"
        << "                            state <= IDLE;\n"
        << "                            results_count <= 0;\n"
        << "                            m_axis_tvalid <= 0;\n"
        << "                            m_axis_tlast <= 0;\n"
        << "                        end else read_ptr <= read_ptr + 1;\n"
        << "                    end\n"
        << "                end\n"
        << "            endcase\n"
        << "        end\n"
        << "    end\n"
        << "endmodule\n";
}

// =============================================================================
// emitTopFPGA - top_fpga.v
// =============================================================================
void Emitter::emitTopFPGA(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top_fpga.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    const size_t numNFAs = nfas.size();
    const std::string busHigh = std::to_string(numNFAs > 0 ? numNFAs - 1 : 0);

    out << "`timescale 1ns / 1ps\n\n";
    out << "// =============================================================================\n"
        << "// top_fpga.v — FPGA Top-Level (Ethernet-based)\n"
        << "// Regex count: " << numNFAs << "\n"
        << "// =============================================================================\n\n";

    out << "module top_fpga #(\n"
        << "    parameter TARGET = \"XILINX\"\n"
        << ")(\n"
        << "    input  wire       clk,\n"
        << "    input  wire       rst_btn,\n\n"
        << "    // Ethernet PHY pins (LAN8720A RMII)\n"
        << "    output wire       eth_mdc,\n"
        << "    inout  wire       eth_mdio,\n"
        << "    output wire       eth_rstn,\n"
        << "    input  wire       eth_crsdv,\n"
        << "    input  wire       eth_rxerr,\n"
        << "    input  wire [1:0] eth_rxd,\n"
        << "    output wire       eth_txen,\n"
        << "    output wire [1:0] eth_txd,\n"
        << "    output wire       eth_refclk,\n\n"
        << "    // UART TX (kept for backward compatibility/debugging)\n"
        << "    output wire       uart_tx_pin,\n\n"
        << "    output wire [" << busHigh << ":0] match_leds\n"
        << ");\n\n";

    out << "    // Ethernet Parameters (from protocol.h context)\n"
        << "    localparam [31:0] LOCAL_IP   = {8'd192, 8'd168, 8'd2, 8'd10};\n"
        << "    localparam [47:0] LOCAL_MAC  = 48'h020000000001;\n"
        << "    localparam [15:0] LOCAL_PORT = 16'd7777;\n"
        << "    localparam [15:0] HOST_PORT  = 16'd7778;\n\n";

    // 50 MHz Clock for Ethernet PHY
    wire clk_50;
    // In a real design, this would be an MMCM. For simulation/generic, we divide.
    out << "    reg eth_refclk_reg = 0;\n"
        << "    always @(posedge clk) eth_refclk_reg <= ~eth_refclk_reg;\n"
        << "    assign clk_50 = eth_refclk_reg;\n"
        << "    assign eth_refclk = clk_50;\n\n";


    out << "    // PHY Reset\n"
        << "    phy_reset_fsm phy_rst_inst (\n"
        << "        .clk(clk), .rst(rst_btn), .eth_rstn(eth_rstn)\n"
        << "    );\n\n";

    out << "    // Ethernet MAC + UDP Stack\n"
        << "    wire [7:0]  rx_udp_payload_tdata;\n"
        << "    wire        rx_udp_payload_tvalid;\n"
        << "    wire        rx_udp_payload_tlast;\n"
        << "    wire        rx_udp_payload_tready;\n\n"
        << "    wire [7:0]  tx_udp_payload_tdata;\n"
        << "    wire        tx_udp_payload_tvalid;\n"
        << "    wire        tx_udp_payload_tlast;\n"
        << "    wire        tx_udp_payload_tready;\n\n"
        << "    // Simplified instantiation of Forencich stack\n"
        << "    // In practice, this requires connecting ARP, IP, UDP modules\n"
        << "    // Here we use a high-level wrapper concept for the emitter output\n"
        << "    udp_complete #(\n"
        << "        .LOCAL_MAC(LOCAL_MAC),\n"
        << "        .LOCAL_IP(LOCAL_IP)\n"
        << "    ) udp_stack_inst (\n"
        << "        .clk(clk), .rst(rst_btn),\n"
        << "        // PHY\n"
        << "        .m_mii_tx_en(eth_txen), .m_mii_txd(eth_txd),\n"
        << "        .s_mii_rx_clk(eth_refclk), .s_mii_rx_dv(eth_crsdv), .s_mii_rxd(eth_rxd), .s_mii_rx_err(eth_rxerr),\n"
        << "        // UDP RX\n"
        << "        .m_udp_payload_tdata(rx_udp_payload_tdata), .m_udp_payload_tvalid(rx_udp_payload_tvalid),\n"
        << "        .m_udp_payload_tlast(rx_udp_payload_tlast), .m_udp_payload_tready(rx_udp_payload_tready),\n"
        << "        .m_udp_port(LOCAL_PORT),\n"
        << "        // UDP TX\n"
        << "        .s_udp_payload_tdata(tx_udp_payload_tdata), .s_udp_payload_tvalid(tx_udp_payload_tvalid),\n"
        << "        .s_udp_payload_tlast(tx_udp_payload_tlast), .s_udp_payload_tready(tx_udp_payload_tready),\n"
        << "        .s_udp_dest_port(HOST_PORT),\n"
        << "        .s_udp_dest_ip(32'hC0A80264) // 192.168.2.100\n"
        << "    );\n\n";

    out << "    // Packet Parser FSM\n"
        << "    wire nfa_en, nfa_rst, nfa_start, nfa_end_of_str;\n"
        << "    wire [7:0] nfa_char_in;\n"
        << "    wire result_valid;\n"
        << "    wire [NUM_REGEX-1:0] result_match;\n"
        << "    wire [31:0] result_seq_num;\n"
        << "    wire [15:0] result_num_strings;\n\n"
        << "    packet_parser_fsm #(.NUM_REGEX(NUM_REGEX)) parser_inst (\n"
        << "        .clk(clk), .rst(rst_btn),\n"
        << "        .udp_payload_tdata(rx_udp_payload_tdata), .udp_payload_tvalid(rx_udp_payload_tvalid),\n"
        << "        .udp_payload_tlast(rx_udp_payload_tlast), .udp_payload_tready(rx_udp_payload_tready),\n"
        << "        .nfa_en(nfa_en), .nfa_rst(nfa_rst), .nfa_start(nfa_start), .nfa_end_of_str(nfa_end_of_str), .nfa_char_in(nfa_char_in),\n"
        << "        .match_bus(match_bus), .result_valid(result_valid), .result_match(result_match),\n"
        << "        .result_seq_num(result_seq_num), .result_num_strings(result_num_strings)\n"
        << "    );\n\n";

    out << "    // NFA Engine\n"
        << "    wire [" << busHigh << ":0] match_bus;\n"
        << "    top regex_engine (\n"
        << "        .clk(clk), .en(nfa_en), .rst(nfa_rst || rst_btn), .start(nfa_start), .end_of_str(nfa_end_of_str), .char_in(nfa_char_in),\n"
        << "        .match_bus(match_bus)\n"
        << "    );\n\n";

    out << "    // Hardware Counters\n"
        << "    reg [15:0] match_count [0:NUM_REGEX-1];\n"
        << "    integer k;\n"
        << "    initial begin\n"
        << "        for (k = 0; k < NUM_REGEX; k = k + 1) match_count[k] = 16'd0;\n"
        << "    end\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst_btn) begin\n"
        << "            for (k = 0; k < NUM_REGEX; k = k + 1) match_count[k] <= 16'd0;\n"
        << "        end else if (result_valid) begin\n"
        << "            for (k = 0; k < NUM_REGEX; k = k + 1)\n"
        << "                if (result_match[k]) match_count[k] <= match_count[k] + 16'd1;\n"
        << "        end\n"
        << "    end\n\n";

    out << "    // Cycle Counter\n"
        << "    wire [31:0] cycle_stamp;\n"
        << "    cycle_counter cycle_cnt_inst (.clk(clk), .rst(rst_btn), .count(cycle_stamp));\n\n";

    out << "    // Result Assembler\n"
        << "    result_assembler #(.NUM_REGEX(NUM_REGEX)) assembler_inst (\n"
        << "        .clk(clk), .rst(rst_btn),\n"
        << "        .result_valid(result_valid), .result_match(result_match),\n"
        << "        .result_seq_num(result_seq_num), .result_num_strings(result_num_strings),\n"
        << "        .cycle_stamp(cycle_stamp),\n";
    for (size_t i = 0; i < numNFAs; ++i) {
        out << "        .match_count_" << i << "(match_count[" << i << "]),\n";
    }
    out << "        .m_axis_tdata(tx_udp_payload_tdata), .m_axis_tvalid(tx_udp_payload_tvalid),\n"
        << "        .m_axis_tlast(tx_udp_payload_tlast), .m_axis_tready(tx_udp_payload_tready)\n"
        << "    );\n\n";

    out << "    assign match_leds = result_match[" << busHigh << ":0];\n\n"
        << "    assign uart_tx_pin = 1'b1; // Default idle\n"
        << "endmodule\n";
}

// =============================================================================
// emitConstraints — constraints.xdc
// =============================================================================
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
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_pin]\n\n"
        << "set_property PACKAGE_PIN D4 [get_ports uart_tx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_pin]\n\n";

    out << "## Buttons (Nexys A7 Center Button - BTNC)\n"
        << "set_property PACKAGE_PIN N17 [get_ports rst_btn]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports rst_btn]\n\n";

    out << "## LAN8720A Ethernet PHY (Nexys A7)\n"
        << "set_property PACKAGE_PIN C9  [get_ports eth_mdc]\n"
        << "set_property PACKAGE_PIN A9  [get_ports eth_mdio]\n"
        << "set_property PACKAGE_PIN D9  [get_ports eth_rstn]\n"
        << "set_property PACKAGE_PIN B3  [get_ports eth_crsdv]\n"
        << "set_property PACKAGE_PIN C3  [get_ports eth_rxerr]\n"
        << "set_property PACKAGE_PIN C10 [get_ports {eth_rxd[0]}]\n"
        << "set_property PACKAGE_PIN C11 [get_ports {eth_rxd[1]}]\n"
        << "set_property PACKAGE_PIN D10 [get_ports eth_txen]\n"
        << "set_property PACKAGE_PIN A10 [get_ports {eth_txd[0]}]\n"
        << "set_property PACKAGE_PIN A8  [get_ports {eth_txd[1]}]\n"
        << "set_property PACKAGE_PIN D11 [get_ports eth_refclk]\n\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports {eth_mdc eth_mdio eth_rstn eth_crsdv eth_rxerr eth_rxd eth_txen eth_txd eth_refclk}]\n\n"
        << "## Ethernet clock constraints\n"
        << "create_clock -period 40.0 -name eth_rx_clk [get_ports eth_crsdv]\n"
        << "set_clock_groups -asynchronous -group [get_clocks sys_clk_pin] -group [get_clocks eth_rx_clk]\n"
        << "set_false_path -from [get_clocks eth_rx_clk] -to [get_clocks sys_clk_pin]\n"
        << "set_false_path -from [get_clocks sys_clk_pin] -to [get_clocks eth_rx_clk]\n\n";

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
