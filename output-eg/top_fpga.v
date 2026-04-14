`timescale 1ns / 1ps

// =============================================================================
// top_fpga.v — FPGA Top-Level (Dynamic Memory-Mapped Version)
// =============================================================================

module top_fpga #(
    parameter CLKS_PER_BIT = 868   // 100 MHz / 115200 baud
)(
    input  wire clk,
    input  wire rst_btn,
    input  wire uart_rx_pin,
    output wire uart_tx_pin,
    output reg  [15:0] match_leds
);

    // UART RX
    wire [7:0] rx_data;
    wire       rx_ready;

    uart_rx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart_rx_inst (
        .clk     (clk),
        .rx      (uart_rx_pin),
        .rx_data (rx_data),
        .rx_ready(rx_ready)
    );

    // Input FIFO
    wire [7:0] fifo_rd_data;
    wire       fifo_empty;
    wire       fifo_full;
    reg        fifo_rd_en = 1'b0;

    uart_rx_fifo #(.DEPTH_LOG2(4)) rx_fifo (
        .clk    (clk),
        .rst    (rst_btn),
        .wr_data(rx_data),
        .wr_en  (rx_ready && !fifo_full),
        .full   (fifo_full),
        .rd_data(fifo_rd_data),
        .rd_en  (fifo_rd_en),
        .empty  (fifo_empty)
    );

    // Dynamic NFA Engine
    reg         nfa_reset      = 1'b0;
    reg         nfa_en         = 1'b0;
    reg [7:0]   nfa_char_in    = 8'h00;
    wire [15:0] match_bus;

    // Programming Signals
    reg         prog_en        = 1'b0;
    reg [3:0]   prog_regex_id  = 4'd0;
    reg [3:0]   prog_state_id  = 4'd0;
    reg [7:0]   prog_char      = 8'd0;
    reg [15:0]  prog_mask      = 16'd0;
    reg [15:0]  prog_accept_mask = 16'd0;
    reg         prog_accept_en = 1'b0;

    dynamic_top #(
        .NUM_NFAS(16),
        .MAX_STATES(16)
    ) regex_engine (
        .clk             (clk),
        .reset           (rst_btn || nfa_reset),
        .en              (nfa_en),
        .char_in         (nfa_char_in),
        .prog_en         (prog_en),
        .prog_regex_id   (prog_regex_id),
        .prog_state_id   (prog_state_id),
        .prog_char       (prog_char),
        .prog_mask       (prog_mask),
        .prog_accept_mask(prog_accept_mask),
        .prog_accept_en  (prog_accept_en),
        .match_bus       (match_bus)
    );

    // Hardware Counters
    reg [31:0] byte_count = 32'd0;
    reg [15:0] match_count [0:15];
    integer ci;
    initial begin
        for (ci = 0; ci < 16; ci = ci + 1)
            match_count[ci] = 16'd0;
    end

    // UART TX Logic (truncated for brevity, logic remains same)
    localparam TX_BUF_LEN = 256;
    reg [7:0] tx_buf [0:TX_BUF_LEN-1];
    reg [7:0] tx_len  = 8'd0;
    reg [7:0] tx_ptr  = 8'd0;
    reg       tx_send = 1'b0;

    wire      tx_busy;
    reg       tx_start_r = 1'b0;
    reg [7:0] tx_data_r  = 8'd0;

    uart_tx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart_tx_inst (
        .clk     (clk),
        .rst     (rst_btn),
        .tx_data (tx_data_r),
        .tx_start(tx_start_r),
        .tx_busy (tx_busy),
        .tx      (uart_tx_pin)
    );

    function [7:0] hex_char;
        input [3:0] nibble;
        begin
            hex_char = (nibble < 4'd10) ? (8'd48 + nibble) : (8'd55 + nibble);
        end
    endfunction

    localparam TX_IDLE = 2'd0, TX_LOAD = 2'd1, TX_WAIT = 2'd2, TX_NEXT = 2'd3;
    reg [1:0] tx_state = TX_IDLE;

    always @(posedge clk) begin
        tx_start_r <= 1'b0;
        if (rst_btn) begin
            tx_state <= TX_IDLE;
            tx_ptr   <= 8'd0;
        end else begin
            case (tx_state)
                TX_IDLE: if (tx_send) begin tx_ptr <= 8'd0; tx_state <= TX_LOAD; end
                TX_LOAD: if (!tx_busy) begin
                    tx_data_r  <= tx_buf[tx_ptr];
                    tx_start_r <= 1'b1;
                    tx_state   <= TX_WAIT;
                end
                TX_WAIT: if (tx_busy)  tx_state <= TX_NEXT;
                TX_NEXT: if (!tx_busy) begin
                    if (tx_ptr + 1 < tx_len) begin
                        tx_ptr   <= tx_ptr + 1;
                        tx_state <= TX_LOAD;
                    end else
                        tx_state <= TX_IDLE;
                end
            endcase
        end
    end

    task build_response;
        input [15:0] mbits;
        input [31:0] bcount;
        integer p, k;
        reg [15:0] tmp16;
        begin
            p = 0;
            tx_buf[p]=8'h4D; p=p+1; // M
            tx_buf[p]=8'h41; p=p+1; // A
            tx_buf[p]=8'h54; p=p+1; // T
            tx_buf[p]=8'h43; p=p+1; // C
            tx_buf[p]=8'h48; p=p+1; // H
            tx_buf[p]=8'h3D; p=p+1; // =
            for (k=15; k>=0; k=k-1) begin
                tx_buf[p] = mbits[k] ? 8'h31 : 8'h30; p=p+1;
            end
            tx_buf[p]=8'h20; p=p+1;
            tx_buf[p]=8'h42; p=p+1; // B
            tx_buf[p]=8'h59; p=p+1; // Y
            tx_buf[p]=8'h54; p=p+1; // T
            tx_buf[p]=8'h45; p=p+1; // E
            tx_buf[p]=8'h53; p=p+1; // S
            tx_buf[p]=8'h3D; p=p+1; // =
            tx_buf[p]=hex_char(bcount[31:28]); p=p+1;
            tx_buf[p]=hex_char(bcount[27:24]); p=p+1;
            tx_buf[p]=hex_char(bcount[23:20]); p=p+1;
            tx_buf[p]=hex_char(bcount[19:16]); p=p+1;
            tx_buf[p]=hex_char(bcount[15:12]); p=p+1;
            tx_buf[p]=hex_char(bcount[11: 8]); p=p+1;
            tx_buf[p]=hex_char(bcount[ 7: 4]); p=p+1;
            tx_buf[p]=hex_char(bcount[ 3: 0]); p=p+1;
            tx_buf[p]=8'h20; p=p+1;
            tx_buf[p]=8'h48; p=p+1; // H
            tx_buf[p]=8'h49; p=p+1; // I
            tx_buf[p]=8'h54; p=p+1; // T
            tx_buf[p]=8'h53; p=p+1; // S
            tx_buf[p]=8'h3D; p=p+1; // =
            for (k=0; k<16; k=k+1) begin
                tmp16 = match_count[k];
                tx_buf[p]=hex_char(tmp16[15:12]); p=p+1;
                tx_buf[p]=hex_char(tmp16[11: 8]); p=p+1;
                tx_buf[p]=hex_char(tmp16[ 7: 4]); p=p+1;
                tx_buf[p]=hex_char(tmp16[ 3: 0]); p=p+1;
                if (k<15) begin tx_buf[p]=8'h2C; p=p+1; end
            end
            tx_buf[p]=8'h0D; p=p+1;
            tx_buf[p]=8'h0A; p=p+1;
            tx_len = p[7:0];
        end
    endtask

    // Main Control FSM States
    localparam S_IDLE       = 4'd0;
    localparam S_FETCH      = 4'd1;
    localparam S_DECODE     = 4'd2;
    localparam S_CHAR_STEP  = 4'd3;
    localparam S_EOL_LATCH  = 4'd4;
    localparam S_TX_ARM     = 4'd5;
    localparam S_TX_WAIT    = 4'd6;
    localparam S_RESET_NFA  = 4'd7;
    localparam S_QUERY_TX   = 4'd8;
    localparam S_MAGIC_1    = 4'd9;
    localparam S_PROG_WAIT  = 4'd10;
    localparam S_PROG_LATCH = 4'd11;

    reg [3:0]  state = S_RESET_NFA;
    reg [15:0] snap_match = 16'b0;
    reg [31:0] snap_bytes = 32'd0;
    
    // Programming State
    reg [17:0] prog_byte_count = 18'd0;
    reg [7:0]  prog_byte_latch = 8'd0;
    
    // Constants for programming

    // Constants for programming
    localparam PROG_TOTAL_BYTES = 18'd131104; // (Keep this at 4 for sim)
    localparam BYTES_PER_REGEX  = 18'd8194; // 2 + 8192

    // --- ADD THESE 3 LINES HERE ---
    reg [15:0] prog_word;
    reg [17:0] regex_offset;
    reg [17:0] trans_offset;

    always @(posedge clk) begin
        tx_send        <= 1'b0;
        fifo_rd_en     <= 1'b0;
        nfa_en         <= 1'b0;
        nfa_reset      <= 1'b0;
        prog_en        <= 1'b0;
        prog_accept_en <= 1'b0;

        if (rst_btn) begin
            state <= S_RESET_NFA;
            match_leds <= 16'b0;
            byte_count <= 32'd0;
            for (ci = 0; ci < 16; ci = ci + 1) match_count[ci] <= 16'd0;
        end else begin
            case (state)
                S_IDLE: begin
                    if (!fifo_empty) state <= S_FETCH;
                end

                S_FETCH: begin
                    fifo_rd_en <= 1'b1;
                    state      <= S_DECODE;
                end

                S_DECODE: begin
                    if (fifo_rd_data == 8'hFE) begin
                        state <= S_MAGIC_1;
                    end else if (fifo_rd_data == 8'h0A || fifo_rd_data == 8'h0D) begin
                        state <= S_EOL_LATCH;
                    end else if (fifo_rd_data == 8'h3F) begin // '?'
                        state <= S_QUERY_TX;
                    end else begin
                        nfa_char_in <= fifo_rd_data;
                        state       <= S_CHAR_STEP;
                    end
                end

                S_MAGIC_1: begin
                    if (!fifo_empty) begin
                        fifo_rd_en <= 1'b1;
                        if (fifo_rd_data == 8'hFE) begin
                            prog_byte_count <= 18'd0;
                            state <= S_PROG_WAIT;
                        end else begin
                            // False alarm, process 0xFE then current byte
                            nfa_char_in <= 8'hFE;
                            nfa_en <= 1'b1;
                            // This is simplified; ideally we'd buffer the second byte
                            state <= S_IDLE;
                        end
                    end
                end

                S_PROG_WAIT: begin
                    if (!fifo_empty) begin
                        fifo_rd_en <= 1'b1;
                        state      <= S_PROG_LATCH;
                    end
                end

                S_PROG_LATCH: begin
                    // Byte is in fifo_rd_data
                    if (prog_byte_count[0] == 1'b0) begin
                        // First byte of a 16-bit word (Little-Endian)
                        prog_byte_latch <= fifo_rd_data;
                    end else begin
                        // Second byte of a 16-bit word
                        prog_word = {fifo_rd_data, prog_byte_latch};
                        
                        // Determine what this word is based on offset within regex slot
                        regex_offset = prog_byte_count % BYTES_PER_REGEX;
                        
                        prog_regex_id <= prog_byte_count / BYTES_PER_REGEX;
                        
                        if (regex_offset == 18'd1) begin
                            // This was the accept_mask (first word of slot)
                            prog_accept_mask <= prog_word;
                            prog_accept_en   <= 1'b1;
                        end else begin
                            // This is a transition mask
                            trans_offset = (regex_offset - 18'd2) / 2; // Index into 16*256
                            prog_state_id <= trans_offset / 256;
                            prog_char     <= trans_offset % 256;
                            prog_mask     <= prog_word;
                            prog_en       <= 1'b1;
                        end
                    end
                    
                    if (prog_byte_count + 1 == PROG_TOTAL_BYTES) begin
                        state <= S_RESET_NFA;
                    end else begin
                        prog_byte_count <= prog_byte_count + 1;
                        state <= S_PROG_WAIT;
                    end
                end

                S_CHAR_STEP: begin
                    nfa_en     <= 1'b1;
                    byte_count <= byte_count + 1;
                    state      <= S_IDLE;
                end

                S_EOL_LATCH: begin
                    snap_match <= match_bus;
                    snap_bytes <= byte_count;
                    match_leds <= match_bus;
                    for (ci = 0; ci < 16; ci = ci + 1)
                        if (match_bus[ci]) match_count[ci] <= match_count[ci] + 16'd1;
                    state <= S_TX_ARM;
                end

                S_TX_ARM: begin
                    build_response(snap_match, snap_bytes);
                    tx_send <= 1'b1;
                    state   <= S_TX_WAIT;
                end

                S_TX_WAIT: if (tx_state == TX_IDLE && !tx_send) state <= S_RESET_NFA;

                S_RESET_NFA: begin
                    nfa_reset <= 1'b1;
                    state     <= S_IDLE;
                end

                S_QUERY_TX: begin
                    build_response(16'b0, byte_count);
                    tx_send <= 1'b1;
                    state   <= S_TX_WAIT;
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
