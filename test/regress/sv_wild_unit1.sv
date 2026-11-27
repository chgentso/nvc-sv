reg [31:0] unit_named = 32'd17;
reg [31:0] unit_wild = 32'd29;

module sv_wild_unit_dummy;
endmodule

module sv_wild_unit_tgt(
  input wire [31:0] unit_named,
  input wire [31:0] unit_wild,
  output wire ok
);
  assign ok = (unit_named == 32'd17) && (unit_wild == 32'd29);
endmodule

module sv_wild_unit_child;
  wire ok;
  sv_wild_unit_tgt u(.unit_named, .ok(ok), .*);

  initial begin
    #1;
    if (ok !== 1'b1) $fatal(1, "expected ok=1");
    unit_named = 32'd18;
    #1;
    if (ok !== 1'b0) $fatal(1, "expected ok=0 after update");
    unit_named = 32'd17;
    #1;
    if (ok !== 1'b1) $fatal(1, "expected ok=1 after restore");
    $display("PASSED");
  end
endmodule

module sv_wild_unit1;
  sv_wild_unit_dummy unit_named();
  sv_wild_unit_dummy unit_wild();
  sv_wild_unit_child c();
endmodule
