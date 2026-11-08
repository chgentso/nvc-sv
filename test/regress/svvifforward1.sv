// Expected values: IEEE 1800-2017 25.9 (virtual interface type, methods through it), 13.5.3 (default argument
// values), 13.5.4 (named arguments), 9.4.2 (posedge event), 9.3.2 (fork-join)
// A virtual interface type used before the interface is declared.

class svvifforward1_dev;
  virtual svvifforward1_if m;
  function void attach(virtual svvifforward1_if v); m = v; m.setH(3); endfunction
  task go(int a); m.send(a); endtask
  function int idle(); return m.isIdle(); endfunction
  function int sum(); return m.cnt + m.get(2) + m.get(); endfunction
  function int sum2(); return m.get(.d(3)); endfunction
  task w(); @(posedge m.clk); m.cnt = 7; endtask
endclass
interface svvifforward1_if;
  logic clk = 0;
  int cnt = 0;
  int h;
  int sent;
  function void setH(int x); h = x; endfunction
  task send(int a); cnt += a; sent = a; endtask
  function bit isIdle(); return cnt == 0; endfunction
  function int get(int d = 1); return h * d; endfunction
  function int plain; return 100; endfunction
  function int pf(); return 1000; endfunction
endinterface
module svvifforward1;
  svvifforward1_if i();
  svvifforward1_dev d = new;
  int errs = 0;
  int id, s1, s2;
  initial begin
    d.attach(i);
    d.go(5);
    if (i.sent !== 5) begin errs++; $error("sent=%0d, expected 5", i.sent); end
    id = d.idle(); s1 = d.sum(); s2 = d.sum2();
    if (id !== 0 || s1 !== 14 || s2 !== 9) begin errs++; $error("idle=%0d sum=%0d sum2=%0d, expected 0 14 9", id, s1, s2); end
    fork d.w(); #5 i.clk = 1; join
    if (i.cnt !== 7) begin errs++; $error("cnt=%0d, expected 7", i.cnt); end
    if (errs == 0) $display("PASSED");
  end
endmodule
