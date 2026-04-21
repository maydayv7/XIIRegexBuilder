`timescale 1ns / 1ps

module top_fpga (
    input  wire clk,        // 100 MHz system clock
    input  wire rst_btn,    // Physical reset button
    input  wire uart_rx_pin,// USB UART RX pin
    output wire uart_tx_pin // USB UART TX pin
);


    wire [7:0] rx_data;
    wire       rx_ready;
    
    uart_rx uart_inst (
        .clk(clk),
        .rx(uart_rx_pin),
        .rx_data(rx_data),
        .rx_ready(rx_ready)
    );

    reg        nfa_start = 1'b1;
    reg  [7:0] nfa_char_in = 8'h00;
    reg        nfa_en = 1'b0;
    wire [6:0] match_bus;
    wire [6:0] active_bus;

    top regex_engine (
        .clk(clk),
        .en(nfa_en),
        .rst(rst_btn),
        .start(nfa_start),
        .char_in(nfa_char_in),
        .match_bus(match_bus),
        .active_bus(active_bus)
    );


    // UART TX Signals
    reg        tx_start = 1'b0;
    reg  [7:0] tx_data = 8'h00;
    wire       tx_busy;

    uart_tx uart_tx_inst (
        .clk(clk),
        .tx_start(tx_start),
        .tx_data(tx_data),
        .tx(uart_tx_pin),
        .tx_busy(tx_busy)
    );

    // 128-Byte Latency Buffering with Precise Alignment
    localparam DELAY_LEN = 128;
    (* ram_style = "distributed" *) reg [7:0] delay_bram [0:DELAY_LEN-1];
    reg [6:0] delay_ptr = 0;
    reg [DELAY_LEN-1:0] commit_history = 0;

    // Per-NFA Active Histories to isolate redaction contexts
    reg [DELAY_LEN-1:0] active_history_0 = 0;
    reg [DELAY_LEN-1:0] active_history_1 = 0;
    reg [DELAY_LEN-1:0] active_history_2 = 0;
    reg [DELAY_LEN-1:0] active_history_3 = 0;
    reg [DELAY_LEN-1:0] active_history_4 = 0;
    reg [DELAY_LEN-1:0] active_history_5 = 0;
    reg [DELAY_LEN-1:0] active_history_6 = 0;

    // Power-On-Reset Initialization
    reg [7:0] por_count = 0;
    wire por_rst = (por_count < 8'hFF);

    reg rx_ready_prev = 0;
    reg [1:0] step = 0;
    reg [7:0] rx_latch = 0;

    always @(posedge clk) begin
        if (rst_btn || por_rst) begin
            if (por_count < 8'h80) begin
                delay_bram[por_count[6:0]] <= 8'h20; // Initialize BRAM sequentially
            end
            if (por_count < 8'hFF) por_count <= por_count + 1;
            
            nfa_en <= 0;
            nfa_start <= 1;
            tx_start <= 0;
            rx_ready_prev <= 0;
            delay_ptr <= 0;
            commit_history <= 0;
            step <= 0;
            active_history_0 <= 0;
            active_history_1 <= 0;
            active_history_2 <= 0;
            active_history_3 <= 0;
            active_history_4 <= 0;
            active_history_5 <= 0;
            active_history_6 <= 0;

        end else begin
            nfa_start <= 0;
            nfa_en <= 0;
            tx_start <= 0;
            rx_ready_prev <= rx_ready;

            // Character Processing Sequence
            if (rx_ready && !rx_ready_prev) begin
                rx_latch <= rx_data;
                step <= 1;
            end

            case (step)
                1: begin
                    // Step 1: Feed NFA Engine
                    nfa_char_in <= rx_latch;
                    nfa_en <= 1;
                    step <= 2;
                end
                2: begin
                    // Step 2: NFA Results and Redaction Output
                    // 1. Read delayed char and check redaction bit (Sampling BEFORE shift)
                    tx_data <= (commit_history[DELAY_LEN-2]) ? 8'h58 : delay_bram[delay_ptr];
                    tx_start <= 1;

                    // 2. Overwrite slot with new character
                    delay_bram[delay_ptr] <= rx_latch;
                    delay_ptr <= delay_ptr + 1;

                    // 3. Update Histories (Isolating contexts)
                    active_history_0 <= active_bus[0] ? {active_history_0[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_1 <= active_bus[1] ? {active_history_1[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_2 <= active_bus[2] ? {active_history_2[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_3 <= active_bus[3] ? {active_history_3[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_4 <= active_bus[4] ? {active_history_4[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_5 <= active_bus[5] ? {active_history_5[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    active_history_6 <= active_bus[6] ? {active_history_6[DELAY_LEN-2:0], 1'b1} : 128'd0;
                    commit_history <= {commit_history[DELAY_LEN-2:0], 1'b0}
                                      | (match_bus[0] ? {active_history_0[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[1] ? {active_history_1[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[2] ? {active_history_2[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[3] ? {active_history_3[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[4] ? {active_history_4[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[5] ? {active_history_5[DELAY_LEN-2:0], 1'b1} : 128'd0)
                                      | (match_bus[6] ? {active_history_6[DELAY_LEN-2:0], 1'b1} : 128'd0);

                    step <= 0;
                end
            endcase
        end
    end
endmodule
