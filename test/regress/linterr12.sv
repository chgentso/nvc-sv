// the interface nested in it.
package svifa
  typedef struct packed { int unsigned A; int unsigned B; } cfg_t;
endpackage

interface linterr12_in #(parameter linterr12_pkg::cfg_t cfg = 0) ();
  typedef struct packed {
    logic [cfg.A-1:0] a;
    logic [cfg.B-1:0] b;
  } a_t;
endinterface

interface linterr12_if #(parameter linterr12_pkg::cfg_t cfg = 0) ();
  localparam int A = cfg.A;
  linterr12_in #(cfg) types ();
  typedef types.a_t a_t;
endinterface

module linterr12_sub(linterr12_if io, output int bits);
  typedef io.a_t a_t;
  localparam int A = io.A;
  a_t x;
  initial begin
    x = '1;
    #1;
    bits = $bits(x) + A;
  end
endmodule

module linterr12;
  localparam linterr12_pkg::cfg_t cfg = '{A: 3, B: 5};
  linterr12_if #(cfg) i();
  int bits;
  linterr12_sub s(i, bits);
  initial begin
    #2;
    if (bits !== 11) $error("FAIL %0d", bits);
    else $display("PASSED");
  end
endmodule
