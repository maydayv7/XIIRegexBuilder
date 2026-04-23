## Clock signal (Nexys A7 100MHz)
set_property PACKAGE_PIN E3 [get_ports clk]
set_property IOSTANDARD LVCMOS33 [get_ports clk]
create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports clk]

## USB-RS232 Interface (Nexys A7)
set_property PACKAGE_PIN C4 [get_ports uart_rx_pin]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_pin]

set_property PACKAGE_PIN D4 [get_ports uart_tx_pin]
set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_pin]

## Buttons (Nexys A7 Center Button - BTNC)
set_property PACKAGE_PIN N17 [get_ports rst_btn]
set_property IOSTANDARD LVCMOS33 [get_ports rst_btn]

## LEDs (Nexys A7 LED0 to LED15)
set_property PACKAGE_PIN H17 [get_ports {match_leds[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[0]}]
set_property PACKAGE_PIN K15 [get_ports {match_leds[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[1]}]
set_property PACKAGE_PIN J13 [get_ports {match_leds[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[2]}]
set_property PACKAGE_PIN N14 [get_ports {match_leds[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[3]}]
set_property PACKAGE_PIN R18 [get_ports {match_leds[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[4]}]
set_property PACKAGE_PIN V17 [get_ports {match_leds[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[5]}]
set_property PACKAGE_PIN U17 [get_ports {match_leds[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[6]}]
