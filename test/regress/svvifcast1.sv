// Expected values: IEEE 1800-2017 25.9 (virtual interface referring to an instance; reads and writes through it
// reach the instance), 6.8 (variable initialiser applied before initial blocks run), 8.10 / 13.4 (calls)
interface IA;
  logic [3:0] addr = 4'h5;
endinterface
typedef virtual IA VIA;
class C;
  VIA m;
  logic [3:0] last;
  function new(input VIA v); m = v; endfunction
  function void show(); last = m.addr; endfunction
endclass
module svvifcast1;
  IA ia(), ib();
  C c, d;
  VIA v;
  int errs = 0;
  logic [3:0] tl;
  task automatic show(VIA x); tl = x.addr; endtask
  task automatic expect4(string what, logic [3:0] got, logic [3:0] exp);
    if (got !== exp) begin errs++; $error("%s: got %h expected %h", what, got, exp); end
  endtask
  initial begin
    c = new(VIA'(ia));
    d = new(VIA'(ib));
    v = VIA'(ib);
    c.show(); expect4("initial", c.last, 4'h5);
    ia.addr = 4'ha;
    ib.addr = 4'h3;
    c.show(); d.show();
    expect4("c", c.last, 4'ha); expect4("d", d.last, 4'h3);
    show(VIA'(ia)); expect4("task ia", tl, 4'ha);
    show(v); expect4("task v", tl, 4'h3);
    v = VIA'(ia);
    v.addr = 4'h7;
    c.show(); expect4("write via v", c.last, 4'h7);
    if (errs == 0) $display("PASSED");
  end
endmodule
