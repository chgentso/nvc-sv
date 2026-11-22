interface cif;
  wire [3:0] dios;
  assign dios[3] = 1'b0;
endinterface

module asic(inout wire POR_N, inout wire IN_N);
  cif chip_if();
  assign POR_N = 1'b1;
  wire seen = IN_N;
  always @(seen) $display("seen %b", seen);
  function automatic logic get_in(); return IN_N; endfunction
endmodule

module vlog_inout_hier_sel;
  asic dut(.POR_N(dut.chip_if.dios[2]), .IN_N(dut.chip_if.dios[3]));
  initial begin
    #5;
    // The port drives one bit and sees the driver of the other
    if (dut.chip_if.dios[2] === 1'b1 && dut.chip_if.dios[3] === 1'b0
        && dut.get_in() === 1'b0 && dut.chip_if.dios[1:0] === 2'bzz)
      $display("PASSED");
    else
      $display("FAILED %b %b", dut.chip_if.dios, dut.get_in());
    $finish;
  end
endmodule
