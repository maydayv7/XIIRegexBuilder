# rebuild_and_program.tcl
# Automation script for XIIRegexBuilder PII Guard

set project_name "PII_Guard_Build"
set project_dir "./vivado_build"
set output_dir "./output"
set part_number "xc7a100tcsg324-1"

# 1. Setup Project
if {[file exists $project_dir]} {
    file delete -force $project_dir
}
create_project $project_name $project_dir -part $part_number

# 2. Add Sources
# Add all generated Verilog files from the output directory
add_files [glob $output_dir/*.v]
# Explicitly exclude the testbench from the hardware build to avoid errors
set_property is_enabled false [get_files $output_dir/tb_top.v]

# Add Constraints
add_files -fileset constrs_1 $output_dir/constraints.xdc

# Set Top Module
set_top top_fpga
update_compile_order -fileset sources_1

# 3. Run Synthesis
launch_runs synth_1 -jobs 8
wait_on_run synth_1

# Check for Synthesis Errors
if {[get_property PROGRESS [get_runs synth_1]] != "100%"} {
    puts "ERROR: Synthesis failed!"
    exit 1
}

# 4. Run Implementation & Bitstream
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1

# Check for Implementation Errors
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} {
    puts "ERROR: Implementation/Bitstream failed!"
    exit 1
}

puts "SUCCESS: Bitstream generated at [get_property DIRECTORY [get_runs impl_1]]/top_fpga.bit"

# 5. Program FPGA
# Note: This requires the FPGA to be DETACHED from WSL and available to Windows Vivado
# OR for Vivado to be running in a mode that can see the hardware server.
open_hw
connect_hw_server
open_hw_target

set device [lindex [get_hw_devices] 0]
current_hw_device $device
refresh_hw_device -update_hw_probes false $device

set_property PROGRAM.FILE "[get_property DIRECTORY [get_runs impl_1]]/top_fpga.bit" $device
program_hw_devices $device
refresh_hw_device $device

puts "SUCCESS: FPGA Programmed!"
close_hw
exit
