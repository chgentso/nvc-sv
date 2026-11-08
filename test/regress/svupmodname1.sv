// Expected values: IEEE 1800-2017 23.11 (bind), 23.8 (upward name resolution of a module-definition name),
// 6.8 / 10.3 (variable initialisers and continuous assignments take effect at time 0)
module leaf;
  logic [3:0] q = 7;
  wire [3:0] n = 4'd5;
endmodule
module core;
  leaf u_leaf();
endmodule
module bound_chk;
  logic [3:0] v;
  wire [3:0] w;
  assign v = core.u_leaf.q;
  assign w = core.u_leaf.n;
  initial begin
    #1;
    if (v !== 4'd7 || w !== 4'd5) $error("v=%0d w=%0d, expected 7 and 5", v, w);
    else $display("PASSED");
  end
endmodule
module svupmodname1;
  core c1();
  bind core bound_chk bc();
  initial #5 $finish;
endmodule
