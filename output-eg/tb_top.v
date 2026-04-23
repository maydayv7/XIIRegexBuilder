`timescale 1ns / 1ps

module tb_top;
    reg clk, en, rst, start, end_of_str;
    reg [7:0] char_in;
    wire [15:0] match_bus;
    wire [15:0] active_bus;

    top uut (.clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match_bus(match_bus), .active_bus(active_bus));

    always #5 clk = ~clk;

    initial begin
        // synthesis translate_off
        `ifndef SYNTHESIS
        $dumpfile("dump.vcd");
        $dumpvars(0, tb_top);
        `endif
        // synthesis translate_on

        clk = 0; en = 1; rst = 1; start = 0; end_of_str = 0; char_in = 0;
        #20 rst = 0; #10;

        // Test case 0: "cat"
        start = 1; #10 start = 0;
        char_in = 8'h63; #10;
        char_in = 8'h61; #10;
        char_in = 8'h74; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000011) begin
            $display("PASS: Test case 0 ('cat') matches expected mask 0000000000000011");
        end else begin
            $display("FAIL: Test case 0 ('cat') expected 0000000000000011, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 1: "dog"
        start = 1; #10 start = 0;
        char_in = 8'h64; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h67; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000100) begin
            $display("PASS: Test case 1 ('dog') matches expected mask 0000000000000100");
        end else begin
            $display("FAIL: Test case 1 ('dog') expected 0000000000000100, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 2: "tomcat"
        start = 1; #10 start = 0;
        char_in = 8'h74; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6d; #10;
        char_in = 8'h63; #10;
        char_in = 8'h61; #10;
        char_in = 8'h74; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 2 ('tomcat') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 2 ('tomcat') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 3: "cot"
        start = 1; #10 start = 0;
        char_in = 8'h63; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h74; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000010) begin
            $display("PASS: Test case 3 ('cot') matches expected mask 0000000000000010");
        end else begin
            $display("FAIL: Test case 3 ('cot') expected 0000000000000010, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 4: "cut"
        start = 1; #10 start = 0;
        char_in = 8'h63; #10;
        char_in = 8'h75; #10;
        char_in = 8'h74; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000010) begin
            $display("PASS: Test case 4 ('cut') matches expected mask 0000000000000010");
        end else begin
            $display("FAIL: Test case 4 ('cut') expected 0000000000000010, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 5: "ct"
        start = 1; #10 start = 0;
        char_in = 8'h63; #10;
        char_in = 8'h74; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 5 ('ct') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 5 ('ct') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 6: "doog"
        start = 1; #10 start = 0;
        char_in = 8'h64; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h67; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000100) begin
            $display("PASS: Test case 6 ('doog') matches expected mask 0000000000000100");
        end else begin
            $display("FAIL: Test case 6 ('doog') expected 0000000000000100, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 7: "dg"
        start = 1; #10 start = 0;
        char_in = 8'h64; #10;
        char_in = 8'h67; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000100) begin
            $display("PASS: Test case 7 ('dg') matches expected mask 0000000000000100");
        end else begin
            $display("FAIL: Test case 7 ('dg') expected 0000000000000100, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 8: "beep"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h65; #10;
        char_in = 8'h65; #10;
        char_in = 8'h70; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000001000) begin
            $display("PASS: Test case 8 ('beep') matches expected mask 0000000000001000");
        end else begin
            $display("FAIL: Test case 8 ('beep') expected 0000000000001000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 9: "bep"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h65; #10;
        char_in = 8'h70; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000001000) begin
            $display("PASS: Test case 9 ('bep') matches expected mask 0000000000001000");
        end else begin
            $display("FAIL: Test case 9 ('bep') expected 0000000000001000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 10: "bp"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h70; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 10 ('bp') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 10 ('bp') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 11: "fly"
        start = 1; #10 start = 0;
        char_in = 8'h66; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h79; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000010000) begin
            $display("PASS: Test case 11 ('fly') matches expected mask 0000000000010000");
        end else begin
            $display("FAIL: Test case 11 ('fly') expected 0000000000010000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 12: "fy"
        start = 1; #10 start = 0;
        char_in = 8'h66; #10;
        char_in = 8'h79; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000010000) begin
            $display("PASS: Test case 12 ('fy') matches expected mask 0000000000010000");
        end else begin
            $display("FAIL: Test case 12 ('fy') expected 0000000000010000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 13: "apple"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h70; #10;
        char_in = 8'h70; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h65; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000100000) begin
            $display("PASS: Test case 13 ('apple') matches expected mask 0000000000100000");
        end else begin
            $display("FAIL: Test case 13 ('apple') expected 0000000000100000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 14: "orange"
        start = 1; #10 start = 0;
        char_in = 8'h6f; #10;
        char_in = 8'h72; #10;
        char_in = 8'h61; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h67; #10;
        char_in = 8'h65; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000100000) begin
            $display("PASS: Test case 14 ('orange') matches expected mask 0000000000100000");
        end else begin
            $display("FAIL: Test case 14 ('orange') expected 0000000000100000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 15: "banana"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h61; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h61; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h61; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 15 ('banana') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 15 ('banana') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 16: "redcar"
        start = 1; #10 start = 0;
        char_in = 8'h72; #10;
        char_in = 8'h65; #10;
        char_in = 8'h64; #10;
        char_in = 8'h63; #10;
        char_in = 8'h61; #10;
        char_in = 8'h72; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000001000000) begin
            $display("PASS: Test case 16 ('redcar') matches expected mask 0000000001000000");
        end else begin
            $display("FAIL: Test case 16 ('redcar') expected 0000000001000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 17: "bluecar"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h75; #10;
        char_in = 8'h65; #10;
        char_in = 8'h63; #10;
        char_in = 8'h61; #10;
        char_in = 8'h72; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000001000000) begin
            $display("PASS: Test case 17 ('bluecar') matches expected mask 0000000001000000");
        end else begin
            $display("FAIL: Test case 17 ('bluecar') expected 0000000001000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 18: "greencar"
        start = 1; #10 start = 0;
        char_in = 8'h67; #10;
        char_in = 8'h72; #10;
        char_in = 8'h65; #10;
        char_in = 8'h65; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h63; #10;
        char_in = 8'h61; #10;
        char_in = 8'h72; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 18 ('greencar') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 18 ('greencar') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 19: "lolo"
        start = 1; #10 start = 0;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000010000000) begin
            $display("PASS: Test case 19 ('lolo') matches expected mask 0000000010000000");
        end else begin
            $display("FAIL: Test case 19 ('lolo') expected 0000000010000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 20: "lololo"
        start = 1; #10 start = 0;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000010000000) begin
            $display("PASS: Test case 20 ('lololo') matches expected mask 0000000010000000");
        end else begin
            $display("FAIL: Test case 20 ('lololo') expected 0000000010000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 21: "lo"
        start = 1; #10 start = 0;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000010000000) begin
            $display("PASS: Test case 21 ('lo') matches expected mask 0000000010000000");
        end else begin
            $display("FAIL: Test case 21 ('lo') expected 0000000010000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 22: "good"
        start = 1; #10 start = 0;
        char_in = 8'h67; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000100000000) begin
            $display("PASS: Test case 22 ('good') matches expected mask 0000000100000000");
        end else begin
            $display("FAIL: Test case 22 ('good') expected 0000000100000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 23: "goodod"
        start = 1; #10 start = 0;
        char_in = 8'h67; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h64; #10;
        char_in = 8'h6f; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000100000000) begin
            $display("PASS: Test case 23 ('goodod') matches expected mask 0000000100000000");
        end else begin
            $display("FAIL: Test case 23 ('goodod') expected 0000000100000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 24: "go"
        start = 1; #10 start = 0;
        char_in = 8'h67; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 24 ('go') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 24 ('go') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 25: "hello"
        start = 1; #10 start = 0;
        char_in = 8'h68; #10;
        char_in = 8'h65; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000001000000000) begin
            $display("PASS: Test case 25 ('hello') matches expected mask 0000001000000000");
        end else begin
            $display("FAIL: Test case 25 ('hello') expected 0000001000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 26: "llo"
        start = 1; #10 start = 0;
        char_in = 8'h6c; #10;
        char_in = 8'h6c; #10;
        char_in = 8'h6f; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000001000000000) begin
            $display("PASS: Test case 26 ('llo') matches expected mask 0000001000000000");
        end else begin
            $display("FAIL: Test case 26 ('llo') expected 0000001000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 27: "he"
        start = 1; #10 start = 0;
        char_in = 8'h68; #10;
        char_in = 8'h65; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 27 ('he') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 27 ('he') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 28: "ad"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0100010000000000) begin
            $display("PASS: Test case 28 ('ad') matches expected mask 0100010000000000");
        end else begin
            $display("FAIL: Test case 28 ('ad') expected 0100010000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 29: "abd"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h62; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000010000000000) begin
            $display("PASS: Test case 29 ('abd') matches expected mask 0000010000000000");
        end else begin
            $display("FAIL: Test case 29 ('abd') expected 0000010000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 30: "acd"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h63; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000010000000000) begin
            $display("PASS: Test case 30 ('acd') matches expected mask 0000010000000000");
        end else begin
            $display("FAIL: Test case 30 ('acd') expected 0000010000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 31: "abcd"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h62; #10;
        char_in = 8'h63; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000010000000000) begin
            $display("PASS: Test case 31 ('abcd') matches expected mask 0000010000000000");
        end else begin
            $display("FAIL: Test case 31 ('abcd') expected 0000010000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 32: "the end"
        start = 1; #10 start = 0;
        char_in = 8'h74; #10;
        char_in = 8'h68; #10;
        char_in = 8'h65; #10;
        char_in = 8'h20; #10;
        char_in = 8'h65; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0001100000000000) begin
            $display("PASS: Test case 32 ('the end') matches expected mask 0001100000000000");
        end else begin
            $display("FAIL: Test case 32 ('the end') expected 0001100000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 33: "end"
        start = 1; #10 start = 0;
        char_in = 8'h65; #10;
        char_in = 8'h6e; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000100000000000) begin
            $display("PASS: Test case 33 ('end') matches expected mask 0000100000000000");
        end else begin
            $display("FAIL: Test case 33 ('end') expected 0000100000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 34: "xay"
        start = 1; #10 start = 0;
        char_in = 8'h78; #10;
        char_in = 8'h61; #10;
        char_in = 8'h79; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0010000000000000) begin
            $display("PASS: Test case 34 ('xay') matches expected mask 0010000000000000");
        end else begin
            $display("FAIL: Test case 34 ('xay') expected 0010000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 35: "xayxby"
        start = 1; #10 start = 0;
        char_in = 8'h78; #10;
        char_in = 8'h61; #10;
        char_in = 8'h79; #10;
        char_in = 8'h78; #10;
        char_in = 8'h62; #10;
        char_in = 8'h79; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0010000000000000) begin
            $display("PASS: Test case 35 ('xayxby') matches expected mask 0010000000000000");
        end else begin
            $display("FAIL: Test case 35 ('xayxby') expected 0010000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 36: "xy"
        start = 1; #10 start = 0;
        char_in = 8'h78; #10;
        char_in = 8'h79; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 36 ('xy') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 36 ('xy') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 37: "ac"
        start = 1; #10 start = 0;
        char_in = 8'h61; #10;
        char_in = 8'h63; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0100000000000000) begin
            $display("PASS: Test case 37 ('ac') matches expected mask 0100000000000000");
        end else begin
            $display("FAIL: Test case 37 ('ac') expected 0100000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 38: "bd"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h64; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0100000000000000) begin
            $display("PASS: Test case 38 ('bd') matches expected mask 0100000000000000");
        end else begin
            $display("FAIL: Test case 38 ('bd') expected 0100000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 39: "bc"
        start = 1; #10 start = 0;
        char_in = 8'h62; #10;
        char_in = 8'h63; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0100000000000000) begin
            $display("PASS: Test case 39 ('bc') matches expected mask 0100000000000000");
        end else begin
            $display("FAIL: Test case 39 ('bc') expected 0100000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 40: "1234"
        start = 1; #10 start = 0;
        char_in = 8'h31; #10;
        char_in = 8'h32; #10;
        char_in = 8'h33; #10;
        char_in = 8'h34; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b1000000000000000) begin
            $display("PASS: Test case 40 ('1234') matches expected mask 1000000000000000");
        end else begin
            $display("FAIL: Test case 40 ('1234') expected 1000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 41: "14"
        start = 1; #10 start = 0;
        char_in = 8'h31; #10;
        char_in = 8'h34; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b1000000000000000) begin
            $display("PASS: Test case 41 ('14') matches expected mask 1000000000000000");
        end else begin
            $display("FAIL: Test case 41 ('14') expected 1000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        // Test case 42: "124"
        start = 1; #10 start = 0;
        char_in = 8'h31; #10;
        char_in = 8'h32; #10;
        char_in = 8'h34; #10;
        end_of_str = 1; #10;
        // Match output is valid on the cycle immediately following end_of_str assertion
        if (match_bus === 16'b0000000000000000) begin
            $display("PASS: Test case 42 ('124') matches expected mask 0000000000000000");
        end else begin
            $display("FAIL: Test case 42 ('124') expected 0000000000000000, got %b", match_bus);
        end
        end_of_str = 0; #10;

        $display("All tests completed.");
        #100; $finish;
    end
endmodule
