// Two bind directives into the same target use the same undeclared name in port connections, and the
// target is instantiated twice: each target instance gets one implicit net (IEEE 1800-2017 6.10, 23.11)
// that both bound instances see, and a name the target declares is not redeclared.
module chk_a(input logic w, input logic d);
  logic ga;
  initial begin #1; ga = w; end
endmodule

module chk_b(input logic w, input logic d);
  logic gb;
  initial begin #1; gb = w; end
endmodule

module tgt(input logic d);
  logic real_sig = 1;
endmodule

bind tgt chk_a u_a(.w(imp), .d(real_sig));
bind tgt chk_b u_b(.w(imp), .d(d));

module svbindimplicit2;
  logic d0 = 0, d1 = 1;
  int errs = 0;
  tgt t0(d0);
  tgt t1(d1);
  initial begin
    #2;
    if (t0.imp !== 1'bz || t1.imp !== 1'bz) begin errs++; $error("imp = %b %b", t0.imp, t1.imp); end
    if (t0.u_a.ga !== 1'bz || t1.u_b.gb !== 1'bz) begin errs++; $error("not z"); end
    if (t0.u_b.d !== 1'b0 || t1.u_b.d !== 1'b1) begin errs++; $error("d = %b %b", t0.u_b.d, t1.u_b.d); end
    if (t0.u_a.d !== 1'b1) begin errs++; $error("real_sig = %b", t0.u_a.d); end
    if (errs == 0) $display("PASSED");
  end
endmodule
