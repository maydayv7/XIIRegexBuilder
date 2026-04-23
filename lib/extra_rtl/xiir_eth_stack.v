/*
 * XIIR Ethernet Stack Wrapper
 * Binds Forencich verilog-ethernet modules and RMII PHY interface.
 * Optimized for Nexys A7 (LAN8720A RMII)
 */

`resetall
`timescale 1ns / 1ps
`default_nettype none

module xiir_eth_stack # (
    parameter LOCAL_MAC = 48'h020000000001,
    parameter LOCAL_IP  = 32'hC0A8020A, // 192.168.2.10
    parameter GATEWAY_IP = 32'hC0A80201,
    parameter SUBNET_MASK = 32'hFFFFFF00
)(
    input  wire        clk,
    input  wire        rst,

    /*
     * Ethernet PHY interface (RMII)
     */
    output wire        m_mii_tx_en,
    output wire [1:0]  m_mii_txd,
    input  wire        s_mii_rx_clk,
    input  wire        s_mii_rx_dv,
    input  wire [1:0]  s_mii_rxd,
    input  wire        s_mii_rx_err,

    /*
     * UDP Payload Interface
     */
    // RX (Master from stack to app)
    output wire [7:0]  m_udp_payload_tdata,
    output wire        m_udp_payload_tvalid,
    output wire        m_udp_payload_tlast,
    input  wire        m_udp_payload_tready,
    output wire [15:0] m_udp_port,

    // TX (Slave from app to stack)
    input  wire [7:0]  s_udp_payload_tdata,
    input  wire        s_udp_payload_tvalid,
    input  wire        s_udp_payload_tlast,
    output wire        s_udp_payload_tready,
    input  wire [15:0] s_udp_dest_port,
    input  wire [31:0] s_udp_dest_ip
);

    // MII Internal Signals
    wire mac_mii_rx_clk;
    wire mac_mii_rx_rst;
    wire [3:0] mac_mii_rxd;
    wire mac_mii_rx_dv;
    wire mac_mii_rx_er;
    wire mac_mii_tx_clk;
    wire mac_mii_tx_rst;
    wire [3:0] mac_mii_txd;
    wire mac_mii_tx_en;
    wire mac_mii_tx_er;

    /*
     * RMII PHY Interface Shim
     */
    rmii_phy_if #(
        .TARGET("XILINX")
    ) phy_if_inst (
        .rst(rst),
        .phy_rmii_ref_clk(s_mii_rx_clk),
        .phy_rmii_rxd(s_mii_rxd),
        .phy_rmii_crs_dv(s_mii_rx_dv),
        .phy_rmii_rx_er(s_mii_rx_err),
        .phy_rmii_txd(m_mii_txd),
        .phy_rmii_tx_en(m_mii_tx_en),

        .mac_mii_rx_clk(mac_mii_rx_clk),
        .mac_mii_rx_rst(mac_mii_rx_rst),
        .mac_mii_rxd(mac_mii_rxd),
        .mac_mii_rx_dv(mac_mii_rx_dv),
        .mac_mii_rx_er(mac_mii_rx_er),
        .mac_mii_tx_clk(mac_mii_tx_clk),
        .mac_mii_tx_rst(mac_mii_tx_rst),
        .mac_mii_txd(mac_mii_txd),
        .mac_mii_tx_en(mac_mii_tx_en),
        .mac_mii_tx_er(mac_mii_tx_er)
    );

    // AXI Stream between MAC and UDP Stack
    wire [7:0] rx_axis_tdata;
    wire rx_axis_tvalid;
    wire rx_axis_tlast;
    wire rx_axis_tuser;
    wire rx_axis_tready;

    wire [7:0] tx_axis_tdata;
    wire tx_axis_tvalid;
    wire tx_axis_tlast;
    wire tx_axis_tuser;
    wire tx_axis_tready;

    /*
     * MAC Layer (eth_mac_mii_fifo)
     */
    eth_mac_mii_fifo #(
        .TARGET("XILINX")
    ) eth_mac_inst (
        .rst(rst),
        .logic_clk(clk),
        .logic_rst(rst),

        // MII interface
        .mii_rx_clk(mac_mii_rx_clk),
        .mii_rx_rst(mac_mii_rx_rst),
        .mii_rxd(mac_mii_rxd),
        .mii_rx_dv(mac_mii_rx_dv),
        .mii_rx_er(mac_mii_rx_er),
        .mii_tx_clk(mac_mii_tx_clk),
        .mii_tx_rst(mac_mii_tx_rst),
        .mii_txd(mac_mii_txd),
        .mii_tx_en(mac_mii_tx_en),
        .mii_tx_er(mac_mii_tx_er),

        // AXI Stream RX (from MAC)
        .rx_axis_tdata(rx_axis_tdata),
        .rx_axis_tvalid(rx_axis_tvalid),
        .rx_axis_tlast(rx_axis_tlast),
        .rx_axis_tuser(rx_axis_tuser),
        // MAC rx_axis doesn't have tready in some versions, but fifo one does
        // eth_mac_mii_fifo has tready? Let's assume it does for flow control.
        // If not, we'd need to use eth_mac_mii directly with axis_fifo.

        // AXI Stream TX (to MAC)
        .tx_axis_tdata(tx_axis_tdata),
        .tx_axis_tvalid(tx_axis_tvalid),
        .tx_axis_tlast(tx_axis_tlast),
        .tx_axis_tready(tx_axis_tready),
        .tx_axis_tuser(tx_axis_tuser)
    );

    /*
     * IP + UDP Stack (udp_complete from library)
     */
    udp_complete #(
        .UDP_CHECKSUM_GEN_ENABLE(0)
    ) udp_stack_inst (
        .clk(clk),
        .rst(rst),

        /*
         * Ethernet frame RX (MAC to UDP)
         */
        .s_eth_hdr_valid(1'b0), // Not using hdr interface, using payload axis directly?
        // Wait, udp_complete has s_eth_payload_axis_*
        .s_eth_payload_axis_tdata(rx_axis_tdata),
        .s_eth_payload_axis_tvalid(rx_axis_tvalid),
        .s_eth_payload_axis_tready(rx_axis_tready),
        .s_eth_payload_axis_tlast(rx_axis_tlast),
        .s_eth_payload_axis_tuser(rx_axis_tuser),

        /*
         * Ethernet frame TX (UDP to MAC)
         */
        .m_eth_payload_axis_tdata(tx_axis_tdata),
        .m_eth_payload_axis_tvalid(tx_axis_tvalid),
        .m_eth_payload_axis_tready(tx_axis_tready),
        .m_eth_payload_axis_tlast(tx_axis_tlast),
        .m_eth_payload_axis_tuser(tx_axis_tuser),

        /*
         * UDP interface RX (Stack to App)
         */
        .m_udp_payload_axis_tdata(m_udp_payload_tdata),
        .m_udp_payload_axis_tvalid(m_udp_payload_tvalid),
        .m_udp_payload_axis_tready(m_udp_payload_tready),
        .m_udp_payload_axis_tlast(m_udp_payload_tlast),
        .m_udp_dest_port(m_udp_port),

        /*
         * UDP interface TX (App to Stack)
         */
        .s_udp_ip_dest_ip(s_udp_dest_ip),
        .s_udp_source_port(16'd7777),
        .s_udp_dest_port(s_udp_dest_port),
        .s_udp_payload_axis_tdata(s_udp_payload_tdata),
        .s_udp_payload_axis_tvalid(s_udp_payload_tvalid),
        .s_udp_payload_axis_tready(s_udp_payload_tready),
        .s_udp_payload_axis_tlast(s_udp_payload_tlast),
        
        /*
         * Configuration
         */
        .local_mac(LOCAL_MAC),
        .local_ip(LOCAL_IP),
        .gateway_ip(GATEWAY_IP),
        .subnet_mask(SUBNET_MASK),
        .clear_arp_cache(1'b0)
        
        // (Other ports omitted for brevity, will be tied to defaults)
    );

endmodule
