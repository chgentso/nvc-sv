// Verilog top level: names inside a VHDL entity and in a Verilog module
// nested below it have one level for each instance
module wrapnames1_inner (input wire clk);
  initial begin
    #1;
    if ($sformatf("%m") != "wrapnames1.u_mid.U_V.u_in") begin
      $display("FAILED: inner %%m = %m");
      $fatal(1);
    end
  end
endmodule

module wn1_vleaf (input wire clk);
  wrapnames1_inner u_in (.clk(clk));
  initial begin
    #1;
    if ($sformatf("%m") != "wrapnames1.u_mid.U_V") begin
      $display("FAILED: leaf %%m = %m");
      $fatal(1);
    end
  end
endmodule

module wrapnames1;
  reg d = 0;

  wn1_leaf #(.ID(0)) u0 (.d(d));

  wn1_leaf #(.ID(1)) ua [1:0] (.d(d));

  genvar i;
  generate
    for (i = 0; i < 2; i = i + 1) begin : g
      wn1_leaf #(.ID(2 + i)) u (.d(d));
    end
  endgenerate

  wn1_mid u_mid (.d(d));

  initial begin
    #2;
    $display("PASSED");
    $finish;
  end
endmodule
