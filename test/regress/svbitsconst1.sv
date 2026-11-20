interface svbits_if #(parameter int W = 8);
  logic [W-1:0] sig;
  logic [3:0] tag;
endinterface

module svbits_sub #(parameter int N = 0, parameter int EXP = 0);
  initial begin
    if (N !== EXP) $fatal(1, "N=%0d exp=%0d", N, EXP);
  end
endmodule

module svbits_bridge #(parameter int EXP = 0) (svbits_if bus);
  svbits_sub #(.N($bits(bus.sig)), .EXP(EXP - 4)) u1 ();
  svbits_sub #(.N($bits({bus.sig, bus.tag})), .EXP(EXP)) u2 ();
endmodule

module svbitsconst1;
  typedef struct {
    logic [7:0] a;
    logic [7:0] b;
  } s_t;
  typedef s_t arr_t[2];

  class C;
    int x;
  endclass

  C c;
  localparam int W_ARR = $bits(arr_t);
  localparam int W_MEM = $bits(c.x);

  svbits_if #(.W(12)) if0 ();
  svbits_bridge #(.EXP(16)) b0 (.bus(if0));

  initial begin
    #1;
    if (W_ARR !== 32) $fatal(1, "W_ARR=%0d", W_ARR);
    if (W_MEM !== 32) $fatal(1, "W_MEM=%0d", W_MEM);
    $display("PASSED");
    $finish;
  end
endmodule
