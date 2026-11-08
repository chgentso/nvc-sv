// Expected values: IEEE 1800-2017 23.3.1/23.10 (several top-level modules, %m prints the hierarchical name of the
// current scope: a top module's name is its path), 9.4.1 (#delay)
module svtops1;
  int err;
  initial begin
    #1;
    if ($sformatf("%m") != "svtops1") begin $error("first %%m '%s'", $sformatf("%m")); err++; end
  end
endmodule

module svtops1b;
  int err;
  initial begin
    #2;
    if ($sformatf("%m") != "svtops1b") begin $error("second %%m '%s'", $sformatf("%m")); err++; end
    #1;
    if (err == 0 && svtops1.err == 0) $display("PASSED");
  end
endmodule
