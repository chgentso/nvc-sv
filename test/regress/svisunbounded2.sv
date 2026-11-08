// $isunbounded is a constant function of a parameter
// IEEE 1800-2017 20.6.3 (range system function: $isunbounded returns true
// if its constant argument is $), 11.2.1 (constant expressions), 6.20.2
module svisunbounded2_sub #(parameter int P = $, parameter bit EXP = 1);
  localparam bit UP = $isunbounded(P);
  initial begin
    #1;
    if (UP !== EXP || $isunbounded(P) !== EXP)
      $display("FAILED %m UP=%b", UP);
    else
      svisunbounded2.ok++;
  end
endmodule

module svisunbounded2;
  int ok;
  svisunbounded2_sub a();
  svisunbounded2_sub #(5, 0) b();
  initial #2 if (ok == 2) $display("PASSED");
endmodule
