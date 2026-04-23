/*
 * RMII PHY interface
 * Converts 2-bit RMII at 50MHz to 4-bit MII at 25MHz
 * Includes alignment logic for nibble boundaries.
 */

`resetall
`timescale 1ns / 1ps
`default_nettype none

module rmii_phy_if #
(
    // target ("SIM", "GENERIC", "XILINX", "ALTERA")
    parameter TARGET = "GENERIC"
)
(
    input  wire        rst,

    /*
     * RMII interface to PHY
     */
    input  wire        phy_rmii_ref_clk,
    input  wire [1:0]  phy_rmii_rxd,
    input  wire        phy_rmii_crs_dv,
    input  wire        phy_rmii_rx_er,
    output wire [1:0]  phy_rmii_txd,
    output wire        phy_rmii_tx_en,

    /*
     * MII interface to MAC
     */
    output wire        mac_mii_rx_clk,
    output wire        mac_mii_rx_rst,
    output wire [3:0]  mac_mii_rxd,
    output wire        mac_mii_rx_dv,
    output wire        mac_mii_rx_er,
    output wire        mac_mii_tx_clk,
    output wire        mac_mii_tx_rst,
    input  wire [3:0]  mac_mii_txd,
    input  wire        mac_mii_tx_en,
    input  wire        mac_mii_tx_er
);

    // RX path: 2-bit to 4-bit
    reg [1:0] rxd_low = 2'b0;
    reg [3:0] rxd_reg = 4'b0;
    reg rxd_dv_reg = 1'b0;
    reg rxd_er_reg = 1'b0;
    reg rx_clk_reg = 1'b0;
    reg rx_phase = 1'b0;

    always @(posedge phy_rmii_ref_clk) begin
        if (rst) begin
            rx_clk_reg <= 0;
            rx_phase <= 0;
            rxd_dv_reg <= 0;
        end else begin
            if (!phy_rmii_crs_dv) begin
                rx_phase <= 0;
                rx_clk_reg <= 0;
                rxd_dv_reg <= 0;
            end else begin
                rx_phase <= ~rx_phase;
                rx_clk_reg <= rx_phase;
                if (!rx_phase) begin
                    rxd_low <= phy_rmii_rxd;
                end else begin
                    rxd_reg <= {phy_rmii_rxd, rxd_low};
                    rxd_dv_reg <= 1'b1;
                    rxd_er_reg <= phy_rmii_rx_er;
                end
            end
        end
    end

    assign mac_mii_rx_clk = rx_clk_reg;
    assign mac_mii_rxd = rxd_reg;
    assign mac_mii_rx_dv = rxd_dv_reg;
    assign mac_mii_rx_er = rxd_er_reg;
    assign mac_mii_rx_rst = rst;

    // TX path: 4-bit to 2-bit
    reg [1:0] txd_high = 2'b0;
    reg [1:0] txd_reg = 2'b0;
    reg tx_en_reg = 1'b0;
    reg tx_clk_reg = 1'b0;
    reg tx_phase = 1'b0;

    always @(posedge phy_rmii_ref_clk) begin
        if (rst) begin
            tx_clk_reg <= 0;
            tx_phase <= 0;
            tx_en_reg <= 0;
        end else begin
            tx_phase <= ~tx_phase;
            tx_clk_reg <= tx_phase;
            if (!tx_phase) begin
                txd_reg <= mac_mii_txd[1:0];
                txd_high <= mac_mii_txd[3:2];
                tx_en_reg <= mac_mii_tx_en;
            end else begin
                txd_reg <= txd_high;
                tx_en_reg <= mac_mii_tx_en;
            end
        end
    end

    assign mac_mii_tx_clk = tx_clk_reg;
    assign phy_rmii_txd = txd_reg;
    assign phy_rmii_tx_en = tx_en_reg;
    assign mac_mii_tx_rst = rst;

endmodule
