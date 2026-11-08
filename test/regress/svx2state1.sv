// Unknown bits are zero when a four-state value is converted to a two-state
// type by assignment, argument, return, cast or port connection.
// Expected values: IEEE 1800-2017 6.11.3, 6.24.1, 5.7.1, 11.4.3, 11.4.11, 11.4.12, 11.5.1, 13.5, 23.3
module svx2state1_sub(input int i, output int o);
  assign o = i;
endmodule
module svx2state1;
  logic [15:0] l = 16'hxz1x;       // only bit 4 is a known 1
  logic [99:0] lw = {100{1'bx}};
  logic [99:0] lw2 = {{98{1'b1}}, 2'bxz};
  wire [15:0] w = 16'hz1zz;        // only bit 8 is a known 1
  int a, c;
  bit [7:0] b;
  bit [99:0] bw, bw2;
  byte by;
  longint li;
  shortint si;
  integer ii = 32'hx1;             // x extends to the left (5.7.1)
  int ret, retg;
  int errs;
  wire [31:0] o;
  svx2state1_sub u(.i(l), .o(o));
  function int f(logic [15:0] x); return x; endfunction
  function bit [7:0] g(logic [15:0] x); return x; endfunction
  bit [3:0] nseen;
  task automatic t(output int r, input bit [3:0] n); r = l; nseen = n; endtask
  task automatic chk(string what, longint got, longint exp);
    if (got !== exp) begin errs++; $error("%s: got %0d expected %0d", what, got, exp); end
  endtask
  initial begin
    errs = 0;
    #1;
    a = l; b = l; by = l; li = l; si = l; bw = lw; bw2 = lw2;
    chk("a", a, 16); chk("b", b, 16); chk("by", by, 16); chk("li", li, 16); chk("si", si, 16);
    if (bw !== 100'd0) begin errs++; $error("bw=%h", bw); end
    if (bw2 !== {{98{1'b1}}, 2'b00}) begin errs++; $error("bw2=%h", bw2); end
    ret = f(l); retg = g(l);
    chk("f", ret, 16); chk("g", retg, 16);
    t(c, l); chk("c", c, 16); chk("n", nseen, 0);
    a = int'(l); chk("cast", a, 16);
    a = w; chk("w", a, 256);
    a = ii; chk("ii", a, 1);
    chk("o", o, 16);
    a = $signed(l); chk("sgn", a, 16);
    a = l + 1; chk("sum", a, 0);
    b = (l == 16'hxz1x) ? 1 : 2; chk("tern", b, 0);
    a = {l, l}; chk("cat", a, 32'h00100010);
    a = l[3:0]; chk("part", a, 0);
    if (errs == 0) $display("PASSED");
  end
endmodule
