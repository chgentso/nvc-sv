package nt1_pkg;
  typedef struct { real v; bit valid; } pt_t;
  function automatic pt_t psum(input pt_t d[]);
    psum.v = 0.0; psum.valid = 0;
    foreach (d[i]) begin psum.v += d[i].v; psum.valid |= d[i].valid; end
  endfunction
  function automatic real rsum(input real d[]);
    rsum = 0.0; foreach (d[i]) rsum += d[i];
  endfunction
endpackage

import nt1_pkg::*;
nettype real rnet with rsum;

module rsrc (input real v, output real o);   // Variable output port
  assign o = v * 2.0;
endmodule

module rio #(parameter real VAL = 1.0) (inout rnet p);
  assign p = VAL;
endmodule

module rsnk (input real i, output real seen);   // Variable input port
  assign seen = i;
endmodule

module vlog_nettype_port1;
  real a = 1.0, b = 0.25, seen1, seen2;
  rnet r;
  rsrc s1 (.v(a), .o(r));
  rsrc s2 (.v(b), .o(r));
  rsnk k1 (.i(r), .seen(seen1));
  rio #(1.5) i1 (.p(node));     // Implicit net takes the nettype of the port
  rio #(2.5) i2 (.p(node));
  rsnk k2 (.i(node), .seen(seen2));
  initial begin
    #1 $display("r=%f seen1=%f seen2=%f", r, seen1, seen2);
    a = 3.0;
    #1 $display("r=%f seen1=%f", r, seen1);
  end
endmodule
