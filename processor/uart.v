`timescale 1ns / 1ps

module uart_rx #(
    parameter CLK_FREQ   = 100000000,
    parameter BAUD_RATE  = 115200,
    parameter FIFO_DEPTH = 16
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       rx,
    output wire [7:0] data,
    output wire       valid,
    input  wire       rd_en
);
  localparam WAIT_LIMIT = CLK_FREQ / BAUD_RATE;

  reg [31:0] count = 0;
  reg [ 3:0] state = 0;
  reg [ 2:0] bit_idx = 0;
  reg rx_sync_0, rx_reg;
  reg [7:0] rx_byte;
  reg       rx_done;

  always @(posedge clk) begin
    rx_sync_0 <= rx;
    rx_reg <= rx_sync_0;
  end

  // --- UART Receiver State Machine ---
  always @(posedge clk) begin
    if (rst) begin
      state   <= 0;
      count   <= 0;
      bit_idx <= 0;
      rx_done <= 0;
    end else begin
      rx_done <= 0;
      case (state)
        0: begin  // Idle
          if (rx_reg == 0) begin
            if (count < WAIT_LIMIT / 2) count <= count + 1;
            else begin
              count <= 0;
              state <= 1;
            end
          end else count <= 0;
        end
        1: begin  // Data Bits
          if (count < WAIT_LIMIT - 1) count <= count + 1;
          else begin
            count <= 0;
            rx_byte[bit_idx] <= rx_reg;
            if (bit_idx == 7) begin
              bit_idx <= 0;
              state   <= 2;
            end else bit_idx <= bit_idx + 1;
          end
        end
        2: begin  // Stop Bit
          if (count < WAIT_LIMIT - 1) count <= count + 1;
          else begin
            count   <= 0;
            rx_done <= 1;
            state   <= 0;
          end
        end
        default: state <= 0;
      endcase
    end
  end

  // --- Simple Synchronous FIFO ---
  localparam ADDR_WIDTH = (FIFO_DEPTH > 1) ? $clog2(FIFO_DEPTH) : 1;
  reg [7:0] fifo_mem[0:FIFO_DEPTH-1];
  reg [ADDR_WIDTH-1:0] wr_ptr = 0;
  reg [ADDR_WIDTH-1:0] rd_ptr = 0;
  reg [$clog2(FIFO_DEPTH+1)-1:0] count_fifo = 0;

  always @(posedge clk) begin
    if (rst) begin
      wr_ptr <= 0;
      rd_ptr <= 0;
      count_fifo <= 0;
    end else begin
      if (rx_done && count_fifo < FIFO_DEPTH) begin
        fifo_mem[wr_ptr] <= rx_byte;
        wr_ptr <= wr_ptr + 1;
        if (!rd_en) count_fifo <= count_fifo + 1;
      end
      if (rd_en && count_fifo > 0) begin
        rd_ptr <= rd_ptr + 1;
        if (!rx_done) count_fifo <= count_fifo - 1;
      end
    end
  end

  assign data  = fifo_mem[rd_ptr];
  assign valid = (count_fifo > 0);

endmodule

module uart_tx #(
    parameter CLK_FREQ  = 100000000,
    parameter BAUD_RATE = 115200
) (
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] data,
    input  wire       send,
    output reg        tx = 1,
    output reg        ready = 1
);
  localparam WAIT_LIMIT = CLK_FREQ / BAUD_RATE;
  reg [31:0] count = 0;
  reg [ 3:0] state = 0;
  reg [ 2:0] bit_idx = 0;
  reg [ 7:0] data_reg;

  always @(posedge clk) begin
    if (rst) begin
      state <= 0;
      tx <= 1;
      ready <= 1;
    end else begin
      case (state)
        0: begin  // Idle
          ready <= 1;
          tx <= 1;
          if (send) begin
            data_reg <= data;
            ready <= 0;
            tx <= 0;  // Start bit
            count <= 0;
            state <= 1;
          end
        end
        1: begin  // Data Bits
          if (count < WAIT_LIMIT - 1) count <= count + 1;
          else begin
            count <= 0;
            tx <= data_reg[bit_idx];
            if (bit_idx == 7) begin
              bit_idx <= 0;
              state   <= 2;
            end else bit_idx <= bit_idx + 1;
          end
        end
        2: begin  // Stop Bit
          if (count < WAIT_LIMIT - 1) count <= count + 1;
          else begin
            count <= 0;
            tx <= 1;
            state <= 0;
          end
        end
        default: state <= 0;
      endcase
    end
  end
endmodule
