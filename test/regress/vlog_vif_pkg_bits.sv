// A virtual interface type whose parameter comes from a constant
// expression on package scoped names is the same type as the one written
// with the folded value (IEEE 1800-2017 6.22.1, 8.25)
interface pp_if #(parameter int H = 32, parameter int D = H) (input wire clk);
  wire [D-1:0] d;
endinterface

class db #(type T);
  static T val;
  static bit has;
  static function void put(T v); val = v; has = 1; endfunction
  static function bit get(output T v); v = val; return has; endfunction
endclass

package q;
  typedef struct packed { logic [191:0] a; logic [1:0] b; } rsp_t;
  typedef union packed { logic [17:0] x; logic [1:0][8:0] y; } un_t;
  parameter int W = 100;
  logic [15:0] vec;
  logic [7:0] arr [5];
endpackage

package p;
  parameter int N1 = $bits(q::rsp_t);               // 194
  parameter int N2 = $bits(q::un_t);                // 18
  parameter int N3 = $clog2(q::W) + 1;              // 8
  parameter int N4 = $size(q::vec);                 // 16
  parameter int N5 = $bits(q::arr);                 // 40
  parameter int N6 = $size(q::arr);                 // 5

  class agent #(parameter int H = 32, parameter int D = H);
    function bit try_get_params(int lit);
      virtual pp_if#(H, D) v;
      return db#(virtual pp_if#(H, D))::get(v);
    endfunction
  endclass

  typedef virtual pp_if#(.D(N1)) vif1_t;
  typedef virtual pp_if#(.D(N2)) vif2_t;
  typedef virtual pp_if#(.D(N3)) vif3_t;
  typedef virtual pp_if#(.D(N4)) vif4_t;
  typedef virtual pp_if#(.D(N5)) vif5_t;
  typedef virtual pp_if#(.D(N6)) vif6_t;
  typedef agent#(.D(N1)) agent1_t;
  typedef agent#(.D(N2)) agent2_t;
  typedef agent#(.D(N3)) agent3_t;
  typedef agent#(.D(N4)) agent4_t;
  typedef agent#(.D(N5)) agent5_t;
  typedef agent#(.D(N6)) agent6_t;
endpackage

module vlog_vif_pkg_bits;
  import p::*;
  bit clk;
  pp_if #(.D(N1)) i1 (.clk(clk));
  pp_if #(.D(N2)) i2 (.clk(clk));
  pp_if #(.D(N3)) i3 (.clk(clk));
  pp_if #(.D(N4)) i4 (.clk(clk));
  pp_if #(.D(N5)) i5 (.clk(clk));
  pp_if #(.D(N6)) i6 (.clk(clk));

  initial begin
    automatic agent1_t a1 = new;
    automatic agent2_t a2 = new;
    automatic agent3_t a3 = new;
    automatic agent4_t a4 = new;
    automatic agent5_t a5 = new;
    automatic agent6_t a6 = new;
    int bad;
    db#(vif1_t)::put(i1);
    db#(vif2_t)::put(i2);
    db#(vif3_t)::put(i3);
    db#(vif4_t)::put(i4);
    db#(vif5_t)::put(i5);
    db#(vif6_t)::put(i6);
    if (!a1.try_get_params(0)) begin $display("FAIL 1"); bad++; end
    if (!a2.try_get_params(0)) begin $display("FAIL 2"); bad++; end
    if (!a3.try_get_params(0)) begin $display("FAIL 3"); bad++; end
    if (!a4.try_get_params(0)) begin $display("FAIL 4"); bad++; end
    if (!a5.try_get_params(0)) begin $display("FAIL 5"); bad++; end
    if (!a6.try_get_params(0)) begin $display("FAIL 6"); bad++; end
    if (bad == 0) $display("PASSED");
    else $display("FAILED");
  end
endmodule
