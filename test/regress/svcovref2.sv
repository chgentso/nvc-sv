// Self-checking test of covergroups with `ref` arguments of 4-state vector and 2-state vector types, a
// value argument used in bin ranges, and a default bin.
// Expected values: IEEE 1800-2017 19.4 (formal arguments, ref tracks the variable), 19.5 (bin ranges built
// from constructor arguments), 19.5 (default bin, not counted in coverage, 19.11), 19.11 (arithmetic).
// A value with x bits matches no bin; whether a default bin collects it is not checked.
`timescale 1ns/1ns
bit ck;
class H; logic [7:0] lv; bit [3:0] nib; int q[$]; int d[]; endclass
module svcovref2;
  logic [7:0] mv; logic [15:0] wide;
  covergroup cg (ref logic [7:0] r, ref bit [3:0] n, input int lo) @(posedge ck);
    cp : coverpoint r { bins a = {[0:lo]}; bins b = {[lo+1:15]}; bins x = default; }
    cn : coverpoint n { bins a = {[0:7]}; bins b = {[8:15]}; }
  endgroup
  H h;
  cg c, c2;
  int fails = 0;
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  initial begin
    h = new; h.d = new[3]; h.q = {1,2,3};
    c = new(mv, h.nib, 7);
    c2 = new(h.lv, h.nib, 7);
    // coverpoint cp has two counted bins (a = [0:7], b = [8:15]); the default bin is not counted
    mv = 3; h.nib = 9; h.lv = 8'h0a; #1 ck = 1; #1 ck = 0;
    chk("a c",  c.get_inst_coverage(),  (50.0 + 50.0) / 2);   // a; cn.b
    chk("a c2", c2.get_inst_coverage(), (50.0 + 50.0) / 2);   // lv=10 -> b; cn.b
    mv = 12; h.nib = 2; h.lv = 8'h90; #1 ck = 1; #1 ck = 0;
    chk("b c",  c.get_inst_coverage(),  (100.0 + 100.0) / 2); // b; cn.a
    chk("b c2", c2.get_inst_coverage(), (50.0 + 100.0) / 2);  // lv=144 -> default only; cn.a
    mv = 8'hxx; #1 ck = 1; #1 ck = 0;
    chk("c c",  c.get_inst_coverage(),  100.0);               // x matches no counted bin
    chk("c c2", c2.get_inst_coverage(), 75.0);                // c2 sampled lv=144, nib=2 again
    if (fails == 0) $display("PASSED");
  end
endmodule
