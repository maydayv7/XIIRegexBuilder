/*
 * Free-running 32-bit cycle counter
 * As per Phase 3, Section 4.4 of implementation.md
 */

module cycle_counter (
    input  wire        clk,
    input  wire        rst,
    output reg [31:0]  count
);

    always @(posedge clk) begin
        if (rst) begin
            count <= 32'b0;
        end else begin
            count <= count + 1;
        end
    end

endmodule
