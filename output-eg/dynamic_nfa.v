`timescale 1ns / 1ps

module dynamic_nfa #(
    parameter MAX_STATES = 16,
    parameter ALPHABET_SIZE = 128
)(
    input wire clk,
    input wire reset,
    input wire en,
    input wire restart,
    input wire [7:0] char_in,
    
    // Programming Interface
    input wire prog_en,
    input wire [3:0] prog_state_id,
    input wire [6:0] prog_char,
    input wire [15:0] prog_mask,
    input wire [15:0] prog_accept_mask_in,
    input wire prog_accept_en,
    
    output wire match_out,
    output wire ready
);

    // Memory: 16 states * 128 characters = 2048 entries
    (* ram_style = "block" *)
    reg [15:0] trans_ram [0:2047];
    
    reg [15:0] accept_mask;
    reg [15:0] current_states;

    // FSM States
    localparam IDLE    = 2'd0;
    localparam PROCESS = 2'd1;
    localparam FINISH  = 2'd2;

    reg [1:0] state;
    reg [4:0] read_idx;
    reg [4:0] accum_idx;
    reg [15:0] next_states_accum;
    reg [15:0] ram_read_data;

    initial begin
        state = IDLE;
        current_states = 16'h1;
        accept_mask = 16'h0;
        read_idx = 0;
        accum_idx = 0;
        next_states_accum = 0;
        ram_read_data = 0;
    end

    // BRAM Port
    wire [10:0] bram_addr = {read_idx[3:0], char_in[6:0]};
    wire [10:0] write_addr = {prog_state_id, prog_char};

    always @(posedge clk) begin
        if (prog_en) begin
            trans_ram[write_addr] <= prog_mask;
        end
        ram_read_data <= trans_ram[bram_addr];
    end

    // FSM and Logic Block - Synchronous Reset
    always @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            current_states <= 16'h1;
            accept_mask <= 16'h0;
            read_idx <= 0;
            accum_idx <= 0;
            next_states_accum <= 0;
        end else if (restart) begin
            current_states <= 16'h1;
            state <= IDLE;
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
                    // Address for next cycle
                    read_idx <= read_idx + 5'd1;

                    // Accumulate data from previous cycle's address
                    if (read_idx > 5'd0) begin
                        if (current_states[accum_idx[3:0]]) begin
                            next_states_accum <= next_states_accum | ram_read_data;
                        end
                        
                        if (accum_idx == 5'd15) begin
                            state <= FINISH;
                        end
                        
                        accum_idx <= accum_idx + 5'd1;
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

    assign match_out = (current_states & accept_mask) != 16'h0;
    assign ready = (state == IDLE);

endmodule
