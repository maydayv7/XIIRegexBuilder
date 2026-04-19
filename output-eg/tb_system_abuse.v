`timescale 1ns / 1ps

module tb_system_abuse;

    reg clk;
    reg rst_btn;
    reg uart_rx_pin;
    wire uart_tx_pin;
    wire [15:0] match_leds;

    // Stable timing for simulation
    localparam CLKS_PER_BIT = 20; 
    localparam PROG_TOTAL_BYTES = 4;

    top_fpga #(
        .CLKS_PER_BIT(CLKS_PER_BIT),
        .PROG_TOTAL_BYTES(PROG_TOTAL_BYTES),
        .FIFO_DEPTH_LOG2(10)
    ) dut (
        .clk(clk),
        .rst_btn(rst_btn),
        .uart_rx_pin(uart_rx_pin),
        .uart_tx_pin(uart_tx_pin),
        .match_leds(match_leds)
    );

    always #5 clk = ~clk; // 100MHz

    // Robust TX Monitor to capture and print characters sent by FPGA
    reg [7:0] tx_char;
    integer j;
    initial tx_char = 0;
    always @(negedge uart_tx_pin) begin
        if (!rst_btn) begin
            #(CLKS_PER_BIT * 10 * 1.5); // Wait for start bit + half of first bit
            for (j = 0; j < 8; j = j + 1) begin
                tx_char[j] = uart_tx_pin;
                #(CLKS_PER_BIT * 10);
            end
            $write("%c", tx_char);
        end
    end

    task send_byte(input [7:0] data);
        integer i;
        begin
            uart_rx_pin = 0; // Start bit
            repeat (CLKS_PER_BIT) @(posedge clk);
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_pin = data[i];
                repeat (CLKS_PER_BIT) @(posedge clk);
            end
            uart_rx_pin = 1; // Stop bit
            repeat (CLKS_PER_BIT) @(posedge clk);
        end
    endtask

    integer count;
    initial begin
        clk = 0;
        rst_btn = 1;
        uart_rx_pin = 1;
        #200 rst_btn = 0;
        #1000;

        $display("--- Starting System Abuse Test ---");
        
        // 1. Send 4-byte programming sequence
        send_byte(8'hFE);
        send_byte(8'hFE);
        send_byte(8'h02); 
        send_byte(8'h00);
        send_byte(8'h00);
        send_byte(8'h00);
        
        $display("Programming complete. Starting data stream...");

        // 2. Batch 1: 50 'a' chars + \n
        for (count = 0; count < 50; count = count + 1) begin
            send_byte(8'd97); // 'a'
        end
        send_byte(8'h0A); // \n
        
        // 3. Batch 2: 50 'b' chars + \n
        for (count = 0; count < 50; count = count + 1) begin
            send_byte(8'd98); // 'b'
        end
        send_byte(8'h0A); // \n
        
        $display("All characters sent. Waiting for responses...");
        
        // Wait long enough for both strings to be transmitted
        #2000000;
        
        $display("\n--- Abuse Test Complete ---");
        $finish;
    end

endmodule
