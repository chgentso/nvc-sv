// The scope given to $dumpvars may be the name of the enclosing module

module svdumpvars1_sub;
  initial $dumpvars(0, svdumpvars1_sub);
endmodule

module svdumpvars1;
  reg a = 0;
  svdumpvars1_sub u();
  initial begin
    $dumpfile("dump.vcd");
    $dumpvars(0, svdumpvars1);
    $dumpvars(1, svdumpvars1.u);
    #1 a = 1;
    if (a === 1'b1) $display("PASSED");
  end
endmodule
