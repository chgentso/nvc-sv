// The scope given to $dumpvars may be a module declared later

module svdumpvars2_sub;
  initial $dumpvars(0, svdumpvars2);
endmodule

module svdumpvars2;
  svdumpvars2_sub u();
  initial begin
    $dumpfile("dump.vcd");
    $dumpvars(0, svdumpvars2);
    #1 $display("PASSED");
  end
endmodule
