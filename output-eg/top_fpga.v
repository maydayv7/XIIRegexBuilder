`timescale 1ns / 1ps

module top_fpga #(
    parameter CLKS_PER_BIT = 868,
    parameter PROG_TOTAL_BYTES = 4098
)(
    input  wire clk,
    input  wire rst_btn,
    input  wire uart_rx_pin,
    output wire uart_tx_pin,
    output wire [15:0] match_leds
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

    // NFA Engine
    reg nfa_en = 0;
    reg nfa_restart = 0;
    reg [7:0] nfa_char_in = 0;
    reg prog_en = 0;
    reg [3:0] prog_state_id = 0;
    reg [6:0] prog_char = 0;
    reg [15:0] prog_mask = 0;
    reg [15:0] prog_accept_mask_in = 0;
    reg prog_accept_en = 0;
    wire match_out;
    wire nfa_ready;

    dynamic_nfa #(
        .MAX_STATES(16),
        .ALPHABET_SIZE(128)
    ) nfa_inst (
        .clk(clk),
        .reset(rst_btn),
        .en(nfa_en),
        .restart(nfa_restart),
        .char_in(nfa_char_in),
        .prog_en(prog_en),
        .prog_state_id(prog_state_id),
        .prog_char(prog_char),
        .prog_mask(prog_mask),
        .prog_accept_mask_in(prog_accept_mask_in),
        .prog_accept_en(prog_accept_en),
        .match_out(match_out),
        .ready(nfa_ready)
    );

    assign match_leds = {15'd0, match_out};

    // UART TX (not strictly used for logic, but kept per instructions)
    reg [7:0] tx_data_r = 0;
    reg tx_start_r = 0;
    wire tx_busy;
    uart_tx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart_tx_inst (
        .clk     (clk),
        .rst     (rst_btn),
        .tx_data (tx_data_r),
        .tx_start(tx_start_r),
        .tx_busy (tx_busy),
        .tx      (uart_tx_pin)
    );

    // Main Control FSM
    localparam S_IDLE      = 4'd0;
    localparam S_FETCH     = 4'd1;
    localparam S_DECODE    = 4'd2;
    localparam S_EXEC_STEP = 4'd3;
    localparam S_PROG_HDR  = 4'd4;
    localparam S_PROG_WAIT = 4'd5;
    localparam S_PROG_LATCH= 4'd6;
    localparam S_PROG_DONE = 4'd7;

    reg [3:0] state = S_IDLE;
    reg [12:0] prog_byte_count = 0;
    reg [7:0]  prog_byte_latch = 0;
    reg [11:0] trans_idx;

    always @(posedge clk) begin
        fifo_rd_en <= 1'b0;
        nfa_en     <= 1'b0;
        nfa_restart <= 1'b0;
        prog_en    <= 1'b0;
        prog_accept_en <= 1'b0;
        tx_start_r <= 1'b0;

        if (rst_btn) begin
            state <= S_IDLE;
            prog_byte_count <= 0;
        end else begin
            case (state)
                S_IDLE: begin
                    // ONLY fetch if the NFA is actually ready for a new character
                    if (!fifo_empty && nfa_ready) begin 
                        fifo_rd_en <= 1'b1;
                        state <= S_FETCH;
                    end
                end

                S_FETCH: state <= S_DECODE;

                S_DECODE: begin
                    if (fifo_rd_data == 8'hFE) begin
                        state <= S_PROG_HDR;
                    end else if (fifo_rd_data == 8'h0A || fifo_rd_data == 8'h0D) begin // \n or \r
                        // String is over. Trigger restart to reset NFA back to State 0
                        nfa_restart <= 1'b1;
                        state <= S_IDLE;
                    end else begin
                        nfa_char_in <= fifo_rd_data;
                        nfa_en <= 1'b1; // Pulse EN
                        state <= S_EXEC_STEP;
                    end
                end

                S_EXEC_STEP: begin
                    nfa_en <= 1'b0; // Drop EN
                    // Wait until NFA drops the ready flag and finishes
                    if (nfa_ready) begin
                        state <= S_IDLE;
                    end
                end

                S_PROG_HDR: begin
                    if (!fifo_empty) begin
                        fifo_rd_en <= 1'b1;
                        state <= S_PROG_WAIT;
                    end
                end

                S_PROG_WAIT: begin
                    // Check for second 0xFE
                    if (fifo_rd_data == 8'hFE) begin
                        prog_byte_count <= 0;
                        state <= S_PROG_LATCH;
                    end else begin
                        // Was just one FE, process it as char if needed, or back to idle
                        nfa_char_in <= 8'hFE;
                        state <= S_EXEC_STEP;
                    end
                end

                S_PROG_LATCH: begin
                    if (!fifo_empty) begin
                        fifo_rd_en <= 1'b1;
                        if (prog_byte_count[0] == 0) begin
                            prog_byte_latch <= fifo_rd_data;
                        end else begin
                            // Combine into 16-bit word
                            if (prog_byte_count == 13'd1) begin
                                prog_accept_mask_in <= {fifo_rd_data, prog_byte_latch};
                                prog_accept_en <= 1'b1;
                            end else begin
                                prog_mask <= {fifo_rd_data, prog_byte_latch};
                                prog_state_id <= (prog_byte_count - 2) >> 8; // (index / 128) * 2 byte logic
                                // Wait, index = (prog_byte_count - 1) / 2
                                // index = 0 -> Word 0 (Accept)
                                // index = 1 -> Word 1 (State 0, Char 0)
                                // word_idx = (prog_byte_count >> 1)
                                // If word_idx == 0 -> Accept mask
                                // If word_idx > 0:
                                //   trans_idx = word_idx - 1
                                //   state = trans_idx / 128
                                //   char = trans_idx % 128
                                begin
                                    trans_idx = (prog_byte_count >> 1) - 1;
                                    prog_state_id <= trans_idx[10:7]; // trans_idx / 128
                                    prog_char     <= trans_idx[6:0];  // trans_idx % 128
                                    prog_en <= 1'b1;
                                end
                            end

                            if (prog_byte_count + 1 >= PROG_TOTAL_BYTES) begin
                                state <= S_IDLE;
                            end
                        end
                        prog_byte_count <= prog_byte_count + 1;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
