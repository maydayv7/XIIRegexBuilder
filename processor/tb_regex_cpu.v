`timescale 1ns / 1ps

module tb_regex_cpu;

  reg         clk;
  reg         rst;
  reg         start;
  reg         end_of_str;
  reg  [ 7:0] char_in;
  reg         char_valid;

  reg         prog_en;
  reg  [ 7:0] prog_addr;
  reg  [31:0] prog_data;

  wire        ready;
  wire [15:0] match_bus;

  regex_cpu #(
      .NUM_REGEX(16)
  ) uut (
      .clk(clk),
      .rst(rst),
      .start(start),
      .end_of_str(end_of_str),
      .char_in(char_in),
      .char_valid(char_valid),
      .prog_en(prog_en),
      .prog_addr(prog_addr),
      .prog_data(prog_data),
      .ready(ready),
      .match_bus(match_bus)
  );

  always #5 clk = ~clk;

  task send_char;
    input [7:0] c;
    begin
      wait (ready);
      @(posedge clk);
      char_in = c;
      char_valid = 1;
      @(posedge clk);
      char_valid = 0;
      @(posedge clk);
      wait (ready);
    end
  endtask

  task finish_string;
    begin
      wait (ready);
      @(posedge clk);
      end_of_str = 1;
      @(posedge clk);
      end_of_str = 0;
      @(posedge clk);
      wait (ready);
    end
  endtask

  task test_string;
    input [127:0] s;
    input integer len;
    integer i;
    reg [7:0] c;
    begin
      @(posedge clk);
      start = 1;
      @(posedge clk);
      start = 0;
      @(posedge clk);
      wait (ready);

      for (i = 0; i < len; i = i + 1) begin
        c = s[(len-1-i)*8+:8];
        send_char(c);
      end
      finish_string();
      $display("Test Done. MatchBus: %b", match_bus);
      #20;
    end
  endtask

  initial begin
    $dumpfile("processor/build/dump_processor.vcd");
    $dumpvars(0, tb_regex_cpu);

    clk = 0;
    rst = 1;
    start = 0;
    end_of_str = 0;
    char_in = 0;
    char_valid = 0;
    prog_en = 0;
    prog_addr = 0;
    prog_data = 0;
    #20 rst = 0;

    $display("--- Starting Robust Simulation ---");

    $display("1. Testing 'abbc' (Regex 0)...");
    test_string("abbc", 4);

    $display("2. Testing 'abb' (Regex 1)...");
    test_string("abb", 3);

    $display("3. Testing 'ac' (Regex 2)...");
    test_string("ac", 2);

    $display("4. Testing 'ccc' (Regex 4)...");
    test_string("ccc", 3);

    $display("5. Testing 'abc' (Regex 7)...");
    test_string("abc", 3);

    $display("6. Testing 'babc' (Regex 9)...");
    test_string("babc", 4);

    $display("7. Testing 'acac' (Regex 10)...");
    test_string("acac", 4);

    $display("8. Testing 'cccb' (Regex 11)...");
    test_string("cccb", 4);

    #50 $finish;
  end

endmodule
