`timescale 1ns / 1ps

module dynamic_top #(
    parameter NUM_NFAS = 16,
    parameter MAX_STATES = 16
)(
    input  wire                   clk,
    input  wire                   reset,
    input  wire                   en,
    input  wire [7:0]             char_in,
    
    // Programming Interface
    input  wire                   prog_en,
    input  wire [3:0]             prog_regex_id,
    input  wire [3:0]             prog_state_id,
    input  wire [7:0]             prog_char,
    input  wire [MAX_STATES-1:0]  prog_mask,
    input  wire [MAX_STATES-1:0]  prog_accept_mask,
    input  wire                   prog_accept_en,
    
    // Status
    output wire [NUM_NFAS-1:0]    match_bus
);

    genvar r;
    generate
        for (r = 0; r < NUM_NFAS; r = r + 1) begin : nfa_instances
            wire inst_prog_en        = prog_en        && (prog_regex_id == r);
            wire inst_prog_accept_en = prog_accept_en && (prog_regex_id == r);
            
            dynamic_nfa #(
                .MAX_STATES(MAX_STATES)
            ) inst (
                .clk             (clk),
                .reset           (reset),
                .en              (en),
                .char_in         (char_in),
                .prog_en         (inst_prog_en),
                .prog_state_id   (prog_state_id),
                .prog_char       (prog_char),
                .prog_mask       (prog_mask),
                .prog_accept_mask(prog_accept_mask),
                .prog_accept_en  (inst_prog_accept_en),
                .match           (match_bus[r])
            );
        end
    endgenerate

endmodule
