// A net of a structure nettype with real and integral members, driven and read through ports
// Expected values: IEEE 1800-2017 6.6.7 (the net takes the value returned by the resolution function called
// with the value of every driver; the data type of a nettype may be a structure of real and integral members),
// 23.3.3.5 (ports of the same nettype merge into one net): drivers {1.5,2,0101} and {0.25,3,0101} resolve to
// {1.75, 5, 0101} (sums of r and n, or of v)
package svnettype9_pkg;
  typedef struct { real r; int n; logic [3:0] v; } mx_t;
  function automatic mx_t res_mx(input mx_t d[]);
    res_mx = '{0.0, 0, 0};
    foreach (d[i]) begin res_mx.r += d[i].r; res_mx.n += d[i].n; res_mx.v |= d[i].v; end
  endfunction
  nettype mx_t mxnet with res_mx;
endpackage
module svnettype9_drv import svnettype9_pkg::*; (output mxnet o, input real r, input int n);
  assign o = '{r, n, 4'b0101};
endmodule
module svnettype9_rd import svnettype9_pkg::*; (input mxnet i, output real r, output int n, output logic [3:0] v);
  assign r = i.r; assign n = i.n; assign v = i.v;
endmodule
module svnettype9;
  import svnettype9_pkg::*;
  mxnet w;
  real rr; int nn; logic [3:0] vv;
  int errors = 0;
  svnettype9_drv d1(.o(w), .r(1.5), .n(2));
  svnettype9_drv d2(.o(w), .r(0.25), .n(3));
  svnettype9_rd rd(.i(w), .r(rr), .n(nn), .v(vv));
  initial begin
    #1;
    if (rr != 1.75 || nn != 5 || vv !== 4'b0101) begin $error("rr %f nn %0d vv %b", rr, nn, vv); errors++; end
    if (w.r != 1.75 || w.n != 5) begin $error("w %f %0d", w.r, w.n); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
