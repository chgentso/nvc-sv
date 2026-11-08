// Self-checking: inline constraints referencing associative arrays and fixed arrays of another object.
// Expected values: IEEE 1800-2017 18.7 (inline constraints), 18.12 (std::randomize), 18.5 (constraints),
// 7.8 (associative arrays), 11.4.3 (modulus).
module svcrvstate2;
  class cfg;
    bit [31:0] mn[int], mx[int];
    int lim[3];
    rand bit [3:0] r;
  endclass
  class tx;
    rand bit [31:0] paddr;
    rand bit [7:0] q;
    bit [31:0] own[int];
    cfg c;
    function bit pick(int index);
      return std::randomize(paddr) with { paddr inside {[c.mn[index]:c.mx[index]]}; paddr % 4 == 0; };
    endfunction
    function bit pick2(int k);
      return randomize(q) with { q < c.lim[k]; q >= c.lim[k] - 3; q > own[2] - 1; };
    endfunction
  endclass
  initial begin
    automatic tx x = new;
    automatic int bad = 0;
    x.c = new;
    x.c.mn[1] = 100; x.c.mx[1] = 200; x.c.mn[5] = 1000; x.c.mx[5] = 1003;
    x.c.lim = '{10, 20, 30};
    x.own[2] = 28;
    repeat (20) begin
      if (!x.pick(1) || x.paddr < 100 || x.paddr > 200 || x.paddr % 4 != 0) bad++;
      if (!x.pick(5) || x.paddr < 1000 || x.paddr > 1003 || x.paddr % 4 != 0) bad++;
      x.own[2] = 5;
      if (!x.pick2(0) || x.q >= 10 || x.q < 7) bad++;
      x.own[2] = 28;
      if (!x.pick2(2) || x.q >= 30 || x.q < 28) bad++;
    end
    if (bad != 0) $error("bad=%0d", bad);
    else $display("PASSED");
  end
endmodule
