set null [vhpi::null]
set root [vhpi::handle RootInst $null]
set dut [vhpi::handle_by_name "dut" $root]
set h_pcomb [vhpi::handle_by_name "pruned_comb" $dut]
set h_pff   [vhpi::handle_by_name "pruned_ff" $dut]

# Run past first rising clock edge (5 ns) while pruned outputs are still pruned
run 8 ns

# Tcl examine & VHPI get_value on pruned outputs (0x05 ^ 0x5a = 0x5f = 01011111, 0x05 + 0x10 = 0x15 = 00010101)
vhpi::assert {[examine /dut/pruned_comb] == {"01011111"}}
vhpi::assert {[examine /dut/pruned_ff]   == {"00010101"}}
vhpi::assert {[vhpi::get_value $h_pcomb] == "01011111"}
vhpi::assert {[vhpi::get_value $h_pff]   == "00010101"}

# Tcl force on pruned outputs + run across din=0x12 (12 ns) and rising edge (15 ns)
force /dut/pruned_comb "11110000"
force /dut/pruned_ff   "10101010"
run 10 ns

vhpi::assert {[examine /dut/pruned_comb] == {"11110000"}}
vhpi::assert {[examine /dut/pruned_ff]   == {"10101010"}}
vhpi::assert {[examine /obs_out]         == {"00010011"}}

# Tcl noforce on island-forced signals must succeed and restore underlying values
noforce /dut/pruned_comb
noforce /dut/pruned_ff
run 2 ns

# din=0x12 -> 0x12 ^ 0x5a = 0x48 = 01001000; 0x12 + 0x10 = 0x22 = 00100010
vhpi::assert {[examine /dut/pruned_comb] == {"01001000"}}
vhpi::assert {[examine /dut/pruned_ff]   == {"00100010"}}

# Run across din returning to 0x05 (32 ns) and rising edge (35 ns)
run 20 ns
vhpi::assert {[examine /obs_out]         == {"00000110"}}
vhpi::assert {[examine /dut/pruned_comb] == {"01011111"}}
vhpi::assert {[examine /dut/pruned_ff]   == {"00010101"}}
