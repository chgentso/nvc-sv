// Self-checking. IEEE 1800-2017 6.20.2, 25.3: a parameter of an interface may
// be a packed structure whose members size the types of the interface and of
// the interface nested in it.
package svifaceparam2_pkg;
  typedef struct packed { int unsigned A; int unsigned B; } cfg_t;
endpackage

interface svifaceparam2_in #(parameter svifaceparam2_pkg::cfg_t cfg = 0) ();
  typedef struct packed {
    logic [cfg.A-1:0] a;
    logic [cfg.B-1:0] b;
  } a_t;
endinterface

interface svifaceparam2_if #(parameter svifaceparam2_pkg::cfg_t cfg = 0) ();
  localparam int A = cfg.A;
  svifaceparam2_in #(cfg) types ();
  typedef types.a_t a_t;
endinterface

module svifaceparam2_sub(svifaceparam2_if io, output int bits);
  typedef io.a_t a_t;
  localparam int A = io.A;
  a_t x;
  initial begin
    x = '1;
    #1;
    bits = $bits(x) + A;
  end
endmodule

module svifaceparam2;
  localparam svifaceparam2_pkg::cfg_t cfg = '{A: 3, B: 5};
  svifaceparam2_if #(cfg) i();
  int bits;
  svifaceparam2_sub s(i, bits);
  initial begin
    #2;
    if (bits !== 11) $error("FAIL %0d", bits);
    else $display("PASSED");
  end
endmodule
