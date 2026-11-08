// Self-checking test of covergroups with `ref` arguments bound to a module variable, an array element, a
// class member of a class that embeds the covergroup, and a member of another object.
// Expected values: IEEE 1800-2017 19.4 (covergroup with formal arguments; a ref argument tracks the
// referenced variable and is evaluated at each sampling event), 19.4.1 (clocking event), 19.8
// (start() on a running covergroup has no effect), 19.11 (bins covered / bins).
`timescale 1ns/1ns
bit ck;
int v;
class holder;
  int m;
  int arr[4];
  covergroup cgm (ref int r) @(posedge ck);
    cp : coverpoint r { bins lo = {[0:9]}; bins hi = {[10:19]}; bins vhi = {[20:29]}; }
  endgroup
  function new(ref int x);
    cgm = new(x);
  endfunction
endclass

module svcovref1;
  int mv;
  int ea[3];
  covergroup cg (ref int r) @(posedge ck);
    cp : coverpoint r { bins lo = {[0:9]}; bins hi = {[10:19]}; bins vhi = {[20:29]}; }
  endgroup
  cg c1 = new(mv);
  cg c2 = new(ea[1]);
  holder h;
  cg c4;
  int fails = 0;
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  task automatic all4(string t, real e1, e2, e3, e4);
    chk({t, " c1 (module var)"}, c1.get_inst_coverage(), e1);
    chk({t, " c2 (array element)"}, c2.get_inst_coverage(), e2);
    chk({t, " h.cgm (class member via ref)"}, h.cgm.get_inst_coverage(), e3);
    chk({t, " c4 (member of another object)"}, c4.get_inst_coverage(), e4);
  endtask
  initial begin
    h = new(v);
    h.cgm.start();
    c4 = new(h.m);
    all4("start", 0.0, 0.0, 0.0, 0.0);
    // each coverpoint has three bins; one new bin is hit per clock event
    mv = 5; ea[1] = 15; v = 25; h.m = 12; #1 ck = 1; #1 ck = 0;
    all4("tick1", 100.0/3, 100.0/3, 100.0/3, 100.0/3);   // lo, hi, vhi, hi
    mv = 15; ea[1] = 5; v = 5; h.m = 25; #1 ck = 1; #1 ck = 0;
    all4("tick2", 200.0/3, 200.0/3, 200.0/3, 200.0/3);   // +hi, +lo, +lo, +vhi
    mv = 25; ea[1] = 25; v = 15; h.m = 5; #1 ck = 1; #1 ck = 0;
    all4("tick3", 100.0, 100.0, 100.0, 100.0);           // +vhi, +vhi, +hi, +lo
    if (fails == 0) $display("PASSED");
    $finish;
  end
endmodule
