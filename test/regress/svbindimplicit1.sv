// A name used in a port connection of a bound instance that is not declared in the target module is an
// implicit net of the target (IEEE 1800-2017 6.10 and 23.11); it must exist in the target instance.
// Reference model: the implicit net w is undriven so reads z; the other connection follows the real signal d.
module chk(input logic w, input logic d);
  logic got_w, got_d;
  initial begin #1; got_w = w; got_d = d; end
endmodule

module tgt(input logic d);
endmodule

bind tgt chk u_chk(.w(w_imp), .d(d));

module svbindimplicit1;
  logic d = 1;
  int errs = 0;
  tgt t(d);
  initial begin
    #2;
    if (t.w_imp !== 1'bz) begin errs++; $error("w_imp = %b", t.w_imp); end
    if (t.u_chk.got_w !== 1'bz) begin errs++; $error("got_w = %b", t.u_chk.got_w); end
    if (t.u_chk.got_d !== 1'b1) begin errs++; $error("got_d = %b", t.u_chk.got_d); end
    if (errs == 0) $display("PASSED");
  end
endmodule
