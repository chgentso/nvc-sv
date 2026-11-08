// Reads and writes through a virtual interface declared with a modport.
// IEEE 1800-2017 25.9: a virtual interface with a modport gives access to the
// items of that modport; 25.5: items listed as inputs can be read, items
// listed as outputs or inouts can also be written, by blocking and
// nonblocking assignment and through selects; the writes are seen through
// the interface instance and through a virtual interface without a modport.
`timescale 1ns/1ns
interface rw_if;
  logic [7:0] a = 8'h11, b, c;
  modport mp(input a, output b, inout c);
endinterface

class drv;
  virtual rw_if.mp vif;
  function new(virtual rw_if.mp v); vif = v; endfunction
  task run();
    vif.b = vif.a + 1;
    vif.c[3:0] = 4'h5;
    vif.c[7:4] = vif.a[3:0];
    #1;
    vif.b <= vif.b + 8'h10;
    #1;
  endtask
endclass

module svvifmodportrw1;
  rw_if i();
  virtual rw_if.mp vm = i.mp;
  virtual rw_if vw = i;
  drv d;
  int errors = 0;

  task automatic check(string what, logic [7:0] got, logic [7:0] exp);
    if (got !== exp) begin
      $display("%s: got %h expected %h", what, got, exp); errors++;
    end
  endtask

  initial begin
    d = new(vm);
    d.run();
    check("i.b", i.b, 8'h22);
    check("i.c", i.c, 8'h15);
    check("vw.b", vw.b, 8'h22);
    check("vm.a", vm.a, 8'h11);
    i.a = 8'h3c;
    #1 check("vm.a after write", vm.a, 8'h3c);
    vm.b[0] = 1'b1;
    vm.c <= vm.a;
    #1;
    check("i.b bit", i.b, 8'h23);
    check("i.c nba", i.c, 8'h3c);
    if (errors == 0) $display("PASSED");
  end
endmodule
