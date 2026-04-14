`timescale 1ns / 1ps

module tb_phase3;
    reg clk;
    reg rst_btn;
    reg uart_rx_pin;
    
    wire uart_tx_pin;
    wire [15:0] match_leds;

    // Instantiate TOP FPGA, but speed up the UART 100x for simulation
    top_fpga #(
        .CLKS_PER_BIT(8) // Normally 868
    ) uut (
        .clk(clk),
        .rst_btn(rst_btn),
        .uart_rx_pin(uart_rx_pin),
        .uart_tx_pin(uart_tx_pin),
        .match_leds(match_leds)
    );

    // 100MHz Clock
    always #5 clk = ~clk;

    // Simulated UART transmission task (matches CLKS_PER_BIT=8)
    task send_byte;
        input [7:0] data;
        integer i;
        begin
            uart_rx_pin = 1'b0; // Start bit
            #(10 * 8);          // 8 clocks per bit * 10ns per clock
            for (i = 0; i < 8; i = i + 1) begin
                uart_rx_pin = data[i]; // Data bits (LSB first)
                #(10 * 8);
            end
            uart_rx_pin = 1'b1; // Stop bit
            #(10 * 8);
            #200; // Small delay between bytes
        end
    endtask

    initial begin
        $dumpfile("dump_phase3.vcd");
        $dumpvars(0, tb_phase3);

        // 1. Initialize
        clk = 0;
        rst_btn = 1;
        uart_rx_pin = 1; // UART idles HIGH
        #100;
        rst_btn = 0;
        #500;

        // 2. Trigger PROGRAM_MODE
        $display("[SIM] Sending Magic Bytes (0xFE 0xFE)...");
        send_byte(8'hFE);
        send_byte(8'hFE);

        // 3. Send 4 bytes of dummy payload
        $display("[SIM] Sending 4 Bytes of Dummy Payload...");
        send_byte(8'hAA); // Byte 1 (Latched)
        send_byte(8'hBB); // Byte 2 (Combined with AA)
        send_byte(8'hCC); // Byte 3 (Latched)
        send_byte(8'hDD); // Byte 4 (Combined with CC -> FSM exits)

        $display("[SIM] Payload sent. Waiting for FSM to settle...");
        #2000; 
        
        $display("[SIM] Phase 3 Test Complete. Open dump_phase3.vcd!");
        $finish;
    end

endmodule