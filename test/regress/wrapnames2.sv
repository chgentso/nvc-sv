// VHDL top level: a Verilog module bound to a component is one level
// named by the component instance and so is a VHDL entity instantiated
// from Verilog
module wn2_inner (input wire d);
  string name;
  initial begin
    #1;
    name = $sformatf("%m");
    if (name != "WRAPNAMES2.G(0).U_V.u_e.U_IN"
        && name != "WRAPNAMES2.G(1).U_V.u_e.U_IN"
        && name != "WRAPNAMES2.U_TOP.u_e.U_IN") begin
      $display("FAILED: inner %%m = %m");
      $fatal(1);
    end
  end
endmodule

module wn2_v (input wire d);
  string name;
  wn2_ent u_e (.d(d));
  initial begin
    #1;
    name = $sformatf("%m");
    if (name != "WRAPNAMES2.G(0).U_V"
        && name != "WRAPNAMES2.G(1).U_V"
        && name != "WRAPNAMES2.U_TOP") begin
      $display("FAILED: module %%m = %m");
      $fatal(1);
    end
  end
endmodule
