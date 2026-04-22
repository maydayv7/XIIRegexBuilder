# VIVADO SYNTHESIS & BITSTREAM SCRIPT
# This script automates the flow for the PII Guard Demo

# 1. Define target part
set part xc7a100tcsg324-1
set outputDir ./build
file mkdir $outputDir

# 2. Create the project in memory
create_project -in_memory -part $part

# 3. Read Verilog Files
read_verilog [glob output/*.v]

# 4. Read Constraints
read_xdc output/constraints.xdc

# 5. Run Synthesis
synth_design -top top_fpga -part $part

# 6. Run Implementation Flow
opt_design
place_design
route_design

# 7. Generate Reports
report_utilization -file $outputDir/post_route_util.txt
report_timing_summary -file $outputDir/post_route_timing.txt

# 8. Generate Bitstream
write_bitstream -force $outputDir/top_fpga.bit

# 9. Cleanup and close
close_project
