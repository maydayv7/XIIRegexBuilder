# VIVADO PROGRAMMING SCRIPT

open_hw_manager
connect_hw_server
open_hw_target

# Get first connected FPGA device
set device [lindex [get_hw_devices] 0]
current_hw_device $device
refresh_hw_device -update_hw_probes false $device

# Set bitstream file and program
if { $argc > 0 } {
    set bitfile [lindex $argv 0]
} else {
    set bitfile "top_fpga.bit"
}
set_property PROGRAM.FILE $bitfile $device
program_hw_devices $device

# Finish and close
close_hw_manager
