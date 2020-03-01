############################################################
## This file is generated automatically by Vivado HLS.
## Please DO NOT edit it.
## Copyright (C) 1986-2019 Xilinx, Inc. All Rights Reserved.
############################################################
open_project vivado_hls_examples
set_top euclidean_dist
add_files vivado_hls_examples/euclidean_dist.c
add_files vivado_hls_examples/reports.csv
add_files vivado_hls_examples/rsqrt.h
add_files vivado_hls_examples/trig.c
open_solution "solution1"
set_part {xc7z020-clg484-1} -tool vivado
create_clock -period 10 -name default
source "./vivado_hls_examples/solution1/directives.tcl"
#csim_design
csynth_design
#cosim_design
export_design -format ip_catalog
