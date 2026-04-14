`timescale 1ns / 1ps

module tb_dynamic_core;
    reg clk, reset, en;
    reg [7:0] char_in;
    
    // Programming signals
    reg prog_en;
    reg [3:0] prog_regex_id;
    reg [3:0] prog_state_id;
    reg [7:0] prog_char;
    reg [15:0] prog_mask;
    reg [15:0] prog_accept_mask;
    reg prog_accept_en;
    
    wire [15:0] match_bus;

    dynamic_top uut (
        .clk(clk),
        .reset(reset),
        .en(en),
        .char_in(char_in),
        .prog_en(prog_en),
        .prog_regex_id(prog_regex_id),
        .prog_state_id(prog_state_id),
        .prog_char(prog_char),
        .prog_mask(prog_mask),
        .prog_accept_mask(prog_accept_mask),
        .prog_accept_en(prog_accept_en),
        .match_bus(match_bus)
    );

    always #5 clk = ~clk;

    initial begin
        // Initialize
        clk = 0; reset = 1; en = 0;
        prog_en = 0; prog_regex_id = 0; prog_state_id = 0;
        prog_char = 0; prog_mask = 0; prog_accept_mask = 0;
        prog_accept_en = 0; char_in = 0;
        
        #20 reset = 0; #10;

        // PROGRAMMING PHASE: Regex "ab"
        // 1. State 0 + 'a' -> State 1 (Bit 1)
        prog_en = 1; prog_regex_id = 0; prog_state_id = 0; 
        prog_char = 8'h61; prog_mask = 16'h0002; #10;
        
        // 2. State 1 + 'b' -> State 2 (Bit 2)
        prog_state_id = 1; prog_char = 8'h62; prog_mask = 16'h0004; #10;
        
        prog_en = 0;
        
        // 3. Set Accept Mask (State 2 is accept)
        prog_accept_en = 1; prog_accept_mask = 16'h0004; #10;
        prog_accept_en = 0; #20;

        // TEST CASE 1: "ab" (Should Match)
        $display("Testing string 'ab'...");
        reset = 1; #10 reset = 0; // Reset state machine to Start State (Bit 0)
        en = 1; char_in = 8'h61; #10; // 'a'
        char_in = 8'h62; #10;         // 'b'
        if (match_bus[0]) $display("PASS: 'ab' matched.");
        else              $display("FAIL: 'ab' did not match.");
        en = 0; #20;

        // TEST CASE 2: "ac" (Should NOT Match)
        $display("Testing string 'ac'...");
        reset = 1; #10 reset = 0;
        en = 1; char_in = 8'h61; #10; // 'a'
        char_in = 8'h63; #10;         // 'c'
        if (!match_bus[0]) $display("PASS: 'ac' did not match.");
        else               $display("FAIL: 'ac' matched (incorrectly).");
        en = 0;

        #100;
        $finish;
    end
endmodule
