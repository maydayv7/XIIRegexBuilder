`timescale 1ns / 1ps

module dynamic_nfa #(
    parameter MAX_STATES = 16,
    parameter ALPHABET_SIZE = 128
)(
    input clk,
    input reset,
    input en,
    input [7:0] char_in,
    
    // Programming Interface
    input prog_en,
    input [3:0] prog_state_id,
    input [6:0] prog_char,
    input [15:0] prog_mask,
    input [15:0] prog_accept_mask_in,
    input prog_accept_en,
    
    output match_out
);

    reg [MAX_STATES-1:0] trans_ram [0:MAX_STATES-1][0:ALPHABET_SIZE-1];
    reg [MAX_STATES-1:0] accept_mask;
    reg [MAX_STATES-1:0] current_states;

    integer s, c;
    initial begin
        for (s = 0; s < MAX_STATES; s = s + 1) begin
            for (c = 0; c < ALPHABET_SIZE; c = c + 1) begin
                trans_ram[s][c] = 16'd0;
            end
        end
        accept_mask = 16'd0;
        current_states = 16'd1; // State 0 active
    end

    // Execution Logic
    wire [6:0] safe_char = char_in[6:0];
    wire [MAX_STATES-1:0] next_states;
    
    // Generate 16 parallel asynchronous reads
    assign next_states = 
        (current_states[0]  ? trans_ram[0][safe_char]  : 16'd0) |
        (current_states[1]  ? trans_ram[1][safe_char]  : 16'd0) |
        (current_states[2]  ? trans_ram[2][safe_char]  : 16'd0) |
        (current_states[3]  ? trans_ram[3][safe_char]  : 16'd0) |
        (current_states[4]  ? trans_ram[4][safe_char]  : 16'd0) |
        (current_states[5]  ? trans_ram[5][safe_char]  : 16'd0) |
        (current_states[6]  ? trans_ram[6][safe_char]  : 16'd0) |
        (current_states[7]  ? trans_ram[7][safe_char]  : 16'd0) |
        (current_states[8]  ? trans_ram[8][safe_char]  : 16'd0) |
        (current_states[9]  ? trans_ram[9][safe_char]  : 16'd0) |
        (current_states[10] ? trans_ram[10][safe_char] : 16'd0) |
        (current_states[11] ? trans_ram[11][safe_char] : 16'd0) |
        (current_states[12] ? trans_ram[12][safe_char] : 16'd0) |
        (current_states[13] ? trans_ram[13][safe_char] : 16'd0) |
        (current_states[14] ? trans_ram[14][safe_char] : 16'd0) |
        (current_states[15] ? trans_ram[15][safe_char] : 16'd0);

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            current_states <= 16'd1;
        end else if (prog_en) begin
            trans_ram[prog_state_id][prog_char] <= prog_mask;
        end else if (prog_accept_en) begin
            accept_mask <= prog_accept_mask_in;
        end else if (en) begin
            current_states <= next_states;
        end
    end

    assign match_out = (current_states & accept_mask) != 0;

endmodule
