`timescale 1ns / 1ps

module top (
    input  wire       clk,
    input  wire       en,
    input  wire       rst,
    input  wire       start,
    input  wire       end_of_str,
    input  wire [7:0] char_in,
    output wire [15:0] match_bus,
    output wire [15:0] active_bus
);

    nfa_0 inst_0 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[0]), .active(active_bus[0])
    );

    nfa_1 inst_1 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[1]), .active(active_bus[1])
    );

    nfa_2 inst_2 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[2]), .active(active_bus[2])
    );

    nfa_3 inst_3 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[3]), .active(active_bus[3])
    );

    nfa_4 inst_4 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[4]), .active(active_bus[4])
    );

    nfa_5 inst_5 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[5]), .active(active_bus[5])
    );

    nfa_6 inst_6 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[6]), .active(active_bus[6])
    );

    nfa_7 inst_7 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[7]), .active(active_bus[7])
    );

    nfa_8 inst_8 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[8]), .active(active_bus[8])
    );

    nfa_9 inst_9 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[9]), .active(active_bus[9])
    );

    nfa_10 inst_10 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[10]), .active(active_bus[10])
    );

    nfa_11 inst_11 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[11]), .active(active_bus[11])
    );

    nfa_12 inst_12 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[12]), .active(active_bus[12])
    );

    nfa_13 inst_13 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[13]), .active(active_bus[13])
    );

    nfa_14 inst_14 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[14]), .active(active_bus[14])
    );

    nfa_15 inst_15 (
        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[15]), .active(active_bus[15])
    );

endmodule
