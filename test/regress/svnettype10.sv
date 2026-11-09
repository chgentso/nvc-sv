// Hierarchical drivers of nets of user-defined nettypes (real and structure data types), and ports of a
// nettype connected to hierarchical names of nets in another instance
// Expected values: IEEE 1800-2017 6.6.7 (a net takes the value returned by the resolution function called
// with every driver, wherever the driver is), 23.3.3.5 (a port connected to a net of the same nettype merges
// with it), 23.6 (hierarchical names; a continuous assignment may drive a net of another instance).
// Real net: 1.0 inside the instance, 2.5 through the output port d1: 3.5; structure net: sums of {5,1},
// {10,1}, {7,1}.
package svnettype10_pkg;
  function automatic real res_sum(input real d[]);
    res_sum = 0.0; foreach (d[i]) res_sum += d[i];
  endfunction
  nettype real rnet with res_sum;
  typedef struct { int sum; int n; } sn_t;
  function automatic sn_t res_sn(input sn_t d[]);
    res_sn = '{0, 0};
    foreach (d[i]) begin res_sn.sum += d[i].sum; res_sn.n += d[i].n; end
  endfunction
  nettype sn_t snet with res_sn;
endpackage
module svnettype10_drv import svnettype10_pkg::*; (output rnet o, input real v);
  assign o = v;
endmodule
module svnettype10_sdrv import svnettype10_pkg::*; (output snet o, input int v);
  assign o = '{v, 1};
endmodule
module svnettype10_leaf import svnettype10_pkg::*; (input rnet i, output real seen);
  assign seen = i;
endmodule
module svnettype10_sub import svnettype10_pkg::*; ();
  rnet net1;
  snet sn1;
  snet sn2;
  assign net1 = 1.0;
  assign sn2 = '{10, 1};
endmodule
module svnettype10;
  import svnettype10_pkg::*;
  int errors = 0;
  svnettype10_sub u();
  real seen1;
  assign u.sn2 = '{5, 1};
  svnettype10_drv d1(.o(u.net1), .v(2.5));
  svnettype10_leaf l1(.i(u.net1), .seen(seen1));
  svnettype10_sdrv sd(.o(u.sn1), .v(7));
  initial begin
    #1;
    if (seen1 != 3.5) begin $error("seen1 %f", seen1); errors++; end
    if (u.net1 != 3.5) begin $error("u.net1 %f", u.net1); errors++; end
    if (u.sn1.sum != 7 || u.sn1.n != 1) begin $error("u.sn1 %0d %0d", u.sn1.sum, u.sn1.n); errors++; end
    if (u.sn2.sum != 15 || u.sn2.n != 2) begin $error("u.sn2 %0d %0d", u.sn2.sum, u.sn2.n); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
