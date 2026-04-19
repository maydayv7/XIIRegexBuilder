`timescale 1ns / 1ps

module tb_dynamic_core;

    reg clk;
    reg reset;
    reg en;
    reg restart;
    reg [7:0] char_in;
    
    reg prog_en;
    reg [3:0] prog_state_id;
    reg [6:0] prog_char;
    reg [15:0] prog_mask;
    reg [15:0] prog_accept_mask_in;
    reg prog_accept_en;
    
    wire match_out;
    wire ready;

    dynamic_nfa #(
        .MAX_STATES(16),
        .ALPHABET_SIZE(128)
    ) dut (
        .clk(clk),
        .reset(reset),
        .en(en),
        .restart(restart),
        .char_in(char_in),
        .prog_en(prog_en),
        .prog_state_id(prog_state_id),
        .prog_char(prog_char),
        .prog_mask(prog_mask),
        .prog_accept_mask_in(prog_accept_mask_in),
        .prog_accept_en(prog_accept_en),
        .match_out(match_out),
        .ready(ready)
    );

    always #5 clk = ~clk;

    initial begin
        clk = 0;
        reset = 1;
        en = 0;
        restart = 0;
        char_in = 0;
        prog_en = 0;
        prog_state_id = 0;
        prog_char = 0;
        prog_mask = 0;
        prog_accept_mask_in = 0;
        prog_accept_en = 0;

        #20 reset = 0;

        // Program "ab"
        // State 0 + 'a' -> State 1
        @(posedge clk);
        prog_en <= 1;
        prog_state_id <= 4'd0;
        prog_char <= 7'd97; // 'a'
        prog_mask <= 16'h0002;
        @(posedge clk);
        
        // State 1 + 'b' -> State 2
        prog_state_id <= 4'd1;
        prog_char <= 7'd98; // 'b'
        prog_mask <= 16'h0004;
        @(posedge clk);
        
        prog_en <= 0;
        
        // Accept Mask = State 2
        prog_accept_en <= 1;
        prog_accept_mask_in <= 16'h0004;
        @(posedge clk);
        prog_accept_en <= 0;

        #20;

        // Test "ab"
        $display("Testing 'ab'...");
        reset = 1; #10 reset = 0; 
        
        @(posedge clk);
        en <= 1;
        char_in <= "a";
        // WAIT FOR THE 18-CYCLE MULTIPLEXER TO FINISH
        repeat (20) @(posedge clk); 
        $display("Char: 'a', Match: %b", match_out);
        
        char_in <= "b";
        // WAIT FOR THE 18-CYCLE MULTIPLEXER TO FINISH
        repeat (20) @(posedge clk); 
        $display("Char: 'b', Match: %b", match_out);
        
        if (match_out === 1'b1)
            $display("Test 'ab': PASS");
        else
            $display("Test 'ab': FAIL");

        #20;

        // Test "ac"
        $display("Testing 'ac'...");
        reset = 1; #10 reset = 0; 
        
        @(posedge clk);
        en <= 1;
        char_in <= "a";
        repeat (20) @(posedge clk); 
        $display("Char: 'a', Match: %b", match_out);
        
        char_in <= "c";
        repeat (20) @(posedge clk); 
        $display("Char: 'c', Match: %b", match_out);
        
        if (match_out === 1'b0)
            $display("Test 'ac': PASS");
        else
            $display("Test 'ac': FAIL");

        $finish;
    end

endmodule
