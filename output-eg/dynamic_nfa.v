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

    // Memory: 16 states * 128 characters
    reg [MAX_STATES-1:0] trans_ram [0:MAX_STATES-1][0:ALPHABET_SIZE-1];
    reg [MAX_STATES-1:0] accept_mask;
    reg [MAX_STATES-1:0] current_states;

    // FSM States
    localparam IDLE    = 2'd0;
    localparam PROCESS = 2'd1;
    localparam FINISH  = 2'd2;

    reg [1:0] state;
    reg [4:0] read_idx;
    reg [4:0] accum_idx;
    reg [15:0] next_states_accum;
    reg [15:0] ram_read_data;

    integer s, c;
    initial begin
        for (s = 0; s < MAX_STATES; s = s + 1) begin
            for (c = 0; c < ALPHABET_SIZE; c = c + 1) begin
                trans_ram[s][c] = 16'd0;
            end
        end
        accept_mask = 16'd0;
        current_states = 16'd1; // State 0 active
        state = IDLE;
    end

    // BRAM Read Block
    // Inferred as Block RAM in Vivado
    always @(posedge clk) begin
        if (prog_en) begin
            trans_ram[prog_state_id][prog_char] <= prog_mask;
        end
        if (state == PROCESS) begin
            ram_read_data <= trans_ram[read_idx[3:0]][char_in[6:0]];
        end
    end

    // State Machine Block
    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            current_states <= 16'd1;
            accept_mask <= 16'd0;
            read_idx <= 0;
            accum_idx <= 0;
            next_states_accum <= 0;
        end else if (prog_accept_en) begin
            accept_mask <= prog_accept_mask_in;
        end else begin
            case (state)
                IDLE: begin
                    if (en) begin
                        state <= PROCESS;
                        read_idx <= 0;
                        accum_idx <= 0;
                        next_states_accum <= 0;
                    end
                end

                PROCESS: begin
                    // Increment read_idx to fetch next word
                    read_idx <= read_idx + 1;
                    
                    // The synchronous RAM read has 1 cycle latency.
                    // When read_idx > 0, ram_read_data contains data for (read_idx - 1),
                    // which corresponds to our current accum_idx.
                    if (read_idx > 5'd0) begin
                        if (current_states[accum_idx[3:0]]) begin
                            next_states_accum <= next_states_accum | ram_read_data;
                        end
                        
                        // Check if we just processed the last state
                        if (accum_idx == MAX_STATES - 1) begin
                            state <= FINISH;
                        end
                        
                        accum_idx <= accum_idx + 1;
                    end
                end

                FINISH: begin
                    current_states <= next_states_accum;
                    state <= IDLE;
                end

                default: state <= IDLE;
            endcase
        end
    end

    assign match_out = (current_states & accept_mask) != 0;

endmodule
