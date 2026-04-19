`timescale 1ns / 1ps

module top_fpga #(
    parameter CLKS_PER_BIT = 868,
    parameter PROG_TOTAL_BYTES = 4098,
    parameter FIFO_DEPTH_LOG2 = 10
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
    uart_rx_fifo #(.DEPTH_LOG2(FIFO_DEPTH_LOG2)) rx_fifo (
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

    // Hardware Counters
    reg [31:0] byte_count = 0;
    reg [15:0] hit_count = 0;

    // TX Buffer and Logic
    reg [7:0] tx_buf [0:63];
    reg [5:0] tx_len = 0;
    reg [5:0] tx_idx = 0;
    reg tx_active = 0;

    function [7:0] nibble_to_ascii(input [3:0] n);
        begin
            if (n < 10) nibble_to_ascii = 8'd48 + n;
            else        nibble_to_ascii = 8'd55 + n;
        end
    endfunction

    task format_response;
        begin
            tx_buf[0]="M"; tx_buf[1]="A"; tx_buf[2]="T"; tx_buf[3]="C"; tx_buf[4]="H"; tx_buf[5]="=";
            tx_buf[6] = match_out ? "1" : "0";
            tx_buf[7]=" "; tx_buf[8]="B"; tx_buf[9]="Y"; tx_buf[10]="T"; tx_buf[11]="E"; tx_buf[12]="S"; tx_buf[13]="=";
            tx_buf[14] = nibble_to_ascii(byte_count[31:28]);
            tx_buf[15] = nibble_to_ascii(byte_count[27:24]);
            tx_buf[16] = nibble_to_ascii(byte_count[23:20]);
            tx_buf[17] = nibble_to_ascii(byte_count[19:16]);
            tx_buf[18] = nibble_to_ascii(byte_count[15:12]);
            tx_buf[19] = nibble_to_ascii(byte_count[11:8]);
            tx_buf[20] = nibble_to_ascii(byte_count[7:4]);
            tx_buf[21] = nibble_to_ascii(byte_count[3:0]);
            tx_buf[22]=" "; tx_buf[23]="H"; tx_buf[24]="I"; tx_buf[25]="T"; tx_buf[26]="S"; tx_buf[27]="=";
            tx_buf[28] = nibble_to_ascii(hit_count[15:12]);
            tx_buf[29] = nibble_to_ascii(hit_count[11:8]);
            tx_buf[30] = nibble_to_ascii(hit_count[7:4]);
            tx_buf[31] = nibble_to_ascii(hit_count[3:0]);
            tx_buf[32] = 8'h0D;
            tx_buf[33] = 8'h0A;
            tx_len = 6'd34;
        end
    endtask

    // UART TX
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

    localparam S_IDLE      = 4'd0;
    localparam S_FETCH     = 4'd1;
    localparam S_DECODE    = 4'd2;
    localparam S_EXEC_STEP = 4'd3;
    localparam S_PROG_HDR  = 4'd4;
    localparam S_PROG_WAIT = 4'd5;
    localparam S_PROG_LATCH= 4'd6;
    localparam S_TX_WAIT   = 4'd8;

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

        if (rst_btn) begin
            state <= S_IDLE;
            prog_byte_count <= 0;
            byte_count <= 0;
            hit_count <= 0;
            tx_active <= 0;
        end else begin
            case (state)
                S_IDLE: begin
                    if (!fifo_empty && nfa_ready && !tx_active) begin 
                        state <= S_FETCH;
                    end
                end

                S_FETCH: begin
                    state <= S_DECODE;
                end

                S_DECODE: begin
                    if (fifo_rd_data == 8'hFE) begin
                        fifo_rd_en <= 1'b1;
                        state <= S_PROG_HDR;
                    end else if (fifo_rd_data == 8'h0A || fifo_rd_data == 8'h0D) begin
                        fifo_rd_en <= 1'b1;
                        if (match_out) hit_count <= hit_count + 16'd1;
                        format_response;
                        tx_active <= 1'b1;
                        nfa_restart <= 1'b1;
                        state <= S_TX_WAIT;
                    end else begin
                        nfa_char_in <= fifo_rd_data;
                        nfa_en <= 1'b1;
                        fifo_rd_en <= 1'b1;
                        byte_count <= byte_count + 32'd1;
                        state <= S_EXEC_STEP;
                    end
                end

                S_EXEC_STEP: begin
                    nfa_en <= 1'b0;
                    if (nfa_ready) begin
                        state <= S_IDLE;
                    end
                end

                S_TX_WAIT: begin
                    if (!tx_active) begin
                        state <= S_IDLE;
                    end
                end

                S_PROG_HDR: begin
                    if (!fifo_empty) state <= S_PROG_WAIT;
                end

                S_PROG_WAIT: begin
                    if (fifo_rd_data == 8'hFE) begin
                        fifo_rd_en <= 1'b1;
                        prog_byte_count <= 0;
                        state <= S_PROG_LATCH;
                    end else begin
                        nfa_char_in <= 8'hFE;
                        nfa_en <= 1'b1;
                        fifo_rd_en <= 1'b1;
                        byte_count <= byte_count + 32'd1;
                        state <= S_EXEC_STEP;
                    end
                end

                S_PROG_LATCH: begin
                    if (!fifo_empty) begin
                        fifo_rd_en <= 1'b1;
                        if (prog_byte_count[0] == 0) begin
                            prog_byte_latch <= fifo_rd_data;
                        end else begin
                            if (prog_byte_count == 13'd1) begin
                                prog_accept_mask_in <= {fifo_rd_data, prog_byte_latch};
                                prog_accept_en <= 1'b1;
                            end else begin
                                prog_mask <= {fifo_rd_data, prog_byte_latch};
                                trans_idx = (prog_byte_count >> 1) - 1;
                                prog_state_id <= trans_idx[10:7];
                                prog_char     <= trans_idx[6:0];
                                prog_en <= 1'b1;
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

    // UART TX Engine
    always @(posedge clk) begin
        if (rst_btn) begin
            tx_idx <= 0;
            tx_start_r <= 0;
        end else if (tx_active) begin
            if (!tx_busy && !tx_start_r) begin
                if (tx_idx < tx_len) begin
                    tx_data_r <= tx_buf[tx_idx];
                    tx_start_r <= 1'b1;
                    tx_idx <= tx_idx + 6'd1;
                end else begin
                    tx_active <= 1'b0;
                    tx_idx <= 0;
                end
            end else begin
                tx_start_r <= 1'b0;
            end
        end
    end

endmodule
