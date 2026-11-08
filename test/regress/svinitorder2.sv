// Expected values: IEEE 1800-2017 9.2.1 (every initial procedure of every instance, generate block and array
// element runs once at time 0), 23.3 (instances, instance arrays), 25.? (interface instances), 27 (generate).
// The order of the initial procedures is not defined (4.7), so the number of executions of each is counted.
package svinitorder2_pkg;
  int hits[string];
  function automatic void hit(string s);
    if (!hits.exists(s)) hits[s] = 0;
    hits[s]++;
  endfunction
endpackage
interface cif #(parameter string N = "i");
  initial svinitorder2_pkg::hit($sformatf("init iface %s", N));
endinterface
module child #(parameter string N = "c");
  initial svinitorder2_pkg::hit($sformatf("init child %s", N));
endmodule
module svinitorder2;
  import svinitorder2_pkg::*;
  int err = 0;
  initial hit("top0");
  child c1();
  initial hit("top1");
  cif #("a") a();
  child #("c2") c2();
  for (genvar i = 0; i < 2; i++) begin : g
    initial hit("g first");
    child #("g") c();
    initial hit("g last");
  end
  initial hit("top2");
  child #("arr") arr[1:0] ();
  initial hit("top3");
  if (1) begin : h
    child #("h") c();
    initial hit("h last");
  end
  initial hit("top4");

  task automatic chk(string k, int n);
    if (!hits.exists(k) || hits[k] != n) begin err++; $error("'%s' ran %0d times, expected %0d", k, hits.exists(k) ? hits[k] : 0, n); end
  endtask
  initial begin
    #1;
    chk("top0", 1); chk("top1", 1); chk("top2", 1); chk("top3", 1); chk("top4", 1);
    chk("init child c", 1); chk("init iface a", 1); chk("init child c2", 1);
    chk("g first", 2); chk("g last", 2); chk("init child g", 2);
    chk("init child arr", 2); chk("init child h", 1); chk("h last", 1);
    if (hits.num() != 14) begin err++; $error("expected 14 distinct processes, got %0d", hits.num()); end
    if (err == 0) $display("PASSED");
  end
endmodule
