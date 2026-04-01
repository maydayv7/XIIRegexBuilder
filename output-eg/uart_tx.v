`timescale 1ns / 1ps


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
