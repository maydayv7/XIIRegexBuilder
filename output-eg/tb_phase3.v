`timescale 1ns / 1ps

module tb_phase3;

    reg clk;
    reg rst_btn;
    reg uart_rx_pin;
    wire uart_tx_pin;
    wire [15:0] match_leds;

    // Override parameters for fast simulation
    top_fpga #(
        .CLKS_PER_BIT(10), // Shorten bit time
        .PROG_TOTAL_BYTES(4) // Only 2 words: Accept Mask + 1 Transition
    ) dut (
        .clk(clk),
        .rst_btn(rst_btn),
        .uart_rx_pin(uart_rx_pin),
        .uart_tx_pin(uart_tx_pin),
        .match_leds(match_leds)
    );

    always #5 clk = ~clk;

    task send_byte(input [7:0] data);
        integer i;
        begin
            // Start bit
            uart_rx_pin = 0;
            repeat (10) @(posedge clk);
            // Data bits
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_pin = data[i];
                repeat (10) @(posedge clk);
            end
            // Stop bit
            uart_rx_pin = 1;
            repeat (10) @(posedge clk);
        end
    endtask

    initial begin
        clk = 0;
        rst_btn = 1;
        uart_rx_pin = 1;
        #100 rst_btn = 0;
        #100;

        $display("Sending Programming Sequence: FE FE AA BB CC DD");
        
        // Header
        send_byte(8'hFE);
        send_byte(8'hFE);
        
        // Payload (4 bytes total as overridden)
        // Word 0: AA BB (Accept Mask = 0xBBAA)
        send_byte(8'hAA);
        send_byte(8'hBB);
        
        // Word 1: CC DD (Transition Mask = 0xDDCC for State 0, Char 0)
        send_byte(8'hCC);
        send_byte(8'hDD);

        #1000;
        
        if (dut.state == 4'd0) // S_IDLE
            $display("SUCCESS: FSM returned to S_IDLE");
        else
            $display("FAILURE: FSM in state %d", dut.state);

        $finish;
    end

endmodule
