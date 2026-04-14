`timescale 1ns / 1ps

module dynamic_nfa #(
    parameter MAX_STATES = 16
)(
    input  wire                   clk,
    input  wire                   reset,
    input  wire                   en,
    input  wire [7:0]             char_in,
    
    // Programming Interface
    input  wire                   prog_en,
    input  wire [3:0]             prog_state_id,
    input  wire [7:0]             prog_char,
    input  wire [MAX_STATES-1:0]  prog_mask,
    input  wire [MAX_STATES-1:0]  prog_accept_mask,
    input  wire                   prog_accept_en,
    
    // Status
    output wire                   match
);


    // Registers (removed inline initialization to avoid conflicts)
    reg [MAX_STATES-1:0] accept_mask;
    reg [MAX_STATES-1:0] current_states;

    // Combinatorial next state logic
    wire [MAX_STATES-1:0] next_state_logic;
    wire [MAX_STATES-1:0] rd_data [0:MAX_STATES-1];

    // Distributed RAM for transitions: [State][Character] -> Next State Mask
    reg [MAX_STATES-1:0] trans_ram [0:MAX_STATES-1][0:255];
    
    // --- CORRECTED INITIALIZATION BLOCK ---
    integer j1, k1;
    initial begin
        // 1. Initialize the distributed RAM to zero
        for (j1 = 0; j1 < MAX_STATES; j1 = j1 + 1) begin
            for (k1 = 0; k1 < 256; k1 = k1 + 1) begin
                trans_ram[j1][k1] = {MAX_STATES{1'b0}};
            end
        end
        
        // 2. Initialize the state registers
        current_states = {{MAX_STATES-1{1'b0}}, 1'b1}; // Sets State 0 to active (1)
        accept_mask = {MAX_STATES{1'b0}};              // Fixed typo here
    end
    
    // Generate 16 parallel asynchronous reads from RAM
    genvar i;
    generate
        for (i = 0; i < MAX_STATES; i = i + 1) begin : gen_rd
            assign rd_data[i] = trans_ram[i][char_in];
        end
    endgenerate

    // Bitwise-OR the transition masks of all currently active states
    reg [MAX_STATES-1:0] combined_next;
    integer j;
    always @(*) begin
        combined_next = {MAX_STATES{1'b0}};
        for (j = 0; j < MAX_STATES; j = j + 1) begin
            if (current_states[j]) begin
                combined_next = combined_next | rd_data[j];
            end
        end
    end
    assign next_state_logic = combined_next;

    // Sequential Logic: Programming and Execution
    always @(posedge clk) begin
        if (reset) begin
            current_states <= 16'h0001; // Start state is always Bit 0
        end else if (prog_en) begin
            trans_ram[prog_state_id][prog_char] <= prog_mask;
        end else if (prog_accept_en) begin
            accept_mask <= prog_accept_mask;
        end else if (en) begin
            current_states <= next_state_logic;
        end
    end

    // Match detection: logic high if any active state is an accept state
    assign match = |(current_states & accept_mask);

endmodule
