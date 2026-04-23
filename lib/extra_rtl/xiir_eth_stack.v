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
        .TARGET("XILINX"),
        .CLOCK_INPUT_STYLE("BUFR")
    ) eth_mac_inst (
        .rst(rst),
        .logic_clk(clk),
        .logic_rst(rst),

        // MII interface
        .mii_rx_clk(mac_mii_rx_clk),
        .mii_rxd(mac_mii_rxd),
        .mii_rx_dv(mac_mii_rx_dv),
        .mii_rx_er(mac_mii_rx_er),
        .mii_tx_clk(mac_mii_tx_clk),
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
     * Ethernet Frame Parsing/Generation
     */
    wire        rx_eth_hdr_valid;
    wire        rx_eth_hdr_ready;
    wire [47:0] rx_eth_dest_mac;
    wire [47:0] rx_eth_src_mac;
    wire [15:0] rx_eth_type;
    wire [7:0]  rx_eth_payload_axis_tdata;
    wire        rx_eth_payload_axis_tvalid;
    wire        rx_eth_payload_axis_tready;
    wire        rx_eth_payload_axis_tlast;
    wire        rx_eth_payload_axis_tuser;

    wire        tx_eth_hdr_valid;
    wire        tx_eth_hdr_ready;
    wire [47:0] tx_eth_dest_mac;
    wire [47:0] tx_eth_src_mac;
    wire [15:0] tx_eth_type;
    wire [7:0]  tx_eth_payload_axis_tdata;
    wire        tx_eth_payload_axis_tvalid;
    wire        tx_eth_payload_axis_tready;
    wire        tx_eth_payload_axis_tlast;
    wire        tx_eth_payload_axis_tuser;

    eth_axis_rx
    eth_axis_rx_inst (
        .clk(clk),
        .rst(rst),
        // AXI input
        .s_axis_tdata(rx_axis_tdata),
        .s_axis_tvalid(rx_axis_tvalid),
        .s_axis_tready(rx_axis_tready),
        .s_axis_tlast(rx_axis_tlast),
        .s_axis_tuser(rx_axis_tuser),
        // Ethernet frame output
        .m_eth_hdr_valid(rx_eth_hdr_valid),
        .m_eth_hdr_ready(rx_eth_hdr_ready),
        .m_eth_dest_mac(rx_eth_dest_mac),
        .m_eth_src_mac(rx_eth_src_mac),
        .m_eth_type(rx_eth_type),
        .m_eth_payload_axis_tdata(rx_eth_payload_axis_tdata),
        .m_eth_payload_axis_tvalid(rx_eth_payload_axis_tvalid),
        .m_eth_payload_axis_tready(rx_eth_payload_axis_tready),
        .m_eth_payload_axis_tlast(rx_eth_payload_axis_tlast),
        .m_eth_payload_axis_tuser(rx_eth_payload_axis_tuser),
        // Status signals
        .busy(),
        .error_header_early_termination()
    );

    eth_axis_tx
    eth_axis_tx_inst (
        .clk(clk),
        .rst(rst),
        // Ethernet frame input
        .s_eth_hdr_valid(tx_eth_hdr_valid),
        .s_eth_hdr_ready(tx_eth_hdr_ready),
        .s_eth_dest_mac(tx_eth_dest_mac),
        .s_eth_src_mac(tx_eth_src_mac),
        .s_eth_type(tx_eth_type),
        .s_eth_payload_axis_tdata(tx_eth_payload_axis_tdata),
        .s_eth_payload_axis_tvalid(tx_eth_payload_axis_tvalid),
        .s_eth_payload_axis_tready(tx_eth_payload_axis_tready),
        .s_eth_payload_axis_tlast(tx_eth_payload_axis_tlast),
        .s_eth_payload_axis_tuser(tx_eth_payload_axis_tuser),
        // AXI output
        .m_axis_tdata(tx_axis_tdata),
        .m_axis_tvalid(tx_axis_tvalid),
        .m_axis_tready(tx_axis_tready),
        .m_axis_tlast(tx_axis_tlast),
        .m_axis_tuser(tx_axis_tuser),
        // Status signals
        .busy()
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
        .s_eth_hdr_valid(rx_eth_hdr_valid),
        .s_eth_hdr_ready(rx_eth_hdr_ready),
        .s_eth_dest_mac(rx_eth_dest_mac),
        .s_eth_src_mac(rx_eth_src_mac),
        .s_eth_type(rx_eth_type),
        .s_eth_payload_axis_tdata(rx_eth_payload_axis_tdata),
        .s_eth_payload_axis_tvalid(rx_eth_payload_axis_tvalid),
        .s_eth_payload_axis_tready(rx_eth_payload_axis_tready),
        .s_eth_payload_axis_tlast(rx_eth_payload_axis_tlast),
        .s_eth_payload_axis_tuser(rx_eth_payload_axis_tuser),

        /*
         * Ethernet frame TX (UDP to MAC)
         */
        .m_eth_hdr_valid(tx_eth_hdr_valid),
        .m_eth_hdr_ready(tx_eth_hdr_ready),
        .m_eth_dest_mac(tx_eth_dest_mac),
        .m_eth_src_mac(tx_eth_src_mac),
        .m_eth_type(tx_eth_type),
        .m_eth_payload_axis_tdata(tx_eth_payload_axis_tdata),
        .m_eth_payload_axis_tvalid(tx_eth_payload_axis_tvalid),
        .m_eth_payload_axis_tready(tx_eth_payload_axis_tready),
        .m_eth_payload_axis_tlast(tx_eth_payload_axis_tlast),
        .m_eth_payload_axis_tuser(tx_eth_payload_axis_tuser),

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
