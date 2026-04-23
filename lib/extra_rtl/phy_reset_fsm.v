/*
 * PHY Reset FSM for LAN8720A
 * As per Phase 1, Section 2.4 of implementation.md
 */

module phy_reset_fsm (
    input  wire clk,
    input  wire rst,
    output reg  eth_rstn
);

    parameter RESET_HOLD_CYCLES    = 1000000; // 10ms @ 100MHz
    parameter RESET_RELEASE_CYCLES = 500000;  // 5ms @ 100MHz

    localparam STATE_RESET_HOLD    = 2'd0;
    localparam STATE_RESET_RELEASE = 2'd1;
    localparam STATE_RUNNING       = 2'd2;

    reg [1:0]  state = STATE_RESET_HOLD;
    reg [23:0] counter = 0;

    always @(posedge clk) begin
        if (rst) begin
            state <= STATE_RESET_HOLD;
            counter <= 0;
            eth_rstn <= 0;
        end else begin
            case (state)
                STATE_RESET_HOLD: begin
                    eth_rstn <= 0;
                    if (counter < RESET_HOLD_CYCLES) begin
                        counter <= counter + 1;
                    end else begin
                        counter <= 0;
                        state <= STATE_RESET_RELEASE;
                    end
                end

                STATE_RESET_RELEASE: begin
                    eth_rstn <= 1;
                    if (counter < RESET_RELEASE_CYCLES) begin
                        counter <= counter + 1;
                    end else begin
                        state <= STATE_RUNNING;
                    end
                end

                STATE_RUNNING: begin
                    eth_rstn <= 1;
                end

                default: state <= STATE_RESET_HOLD;
            endcase
        end
    end

endmodule
