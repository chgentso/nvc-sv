// Self-checking test of cross bins with `with` on a tuple-level expression and with a conjunction of
// binsof expressions.
// Expected values: IEEE 1800-2017 19.6.1 (user-defined cross bins; cross products selected by no user bin
// remain automatic bins; a product selected by two user bins counts in both), 19.6.1.1 (with, intersect,
// &&), 19.8 (covered/total), 19.11 (covered / total; covergroup = average of ca, cb, cx).
// ca and cb have 4 automatic bins each (16 products); w = {a + b > 3} has 6 products, w2 = {b < 2 and
// a == 3} has 2, one shared, so 7 products are selected and the cross has 16 - 7 + 2 = 11 bins.
module svcovxwith2;
  bit [1:0] a, b; bit en;
  covergroup g;
    ca: coverpoint a;
    cb: coverpoint b;
    cx: cross ca, cb { bins w = binsof(ca) with (ca + cb > 3); bins w2 = binsof(cb) with (cb < 2) && binsof(ca) intersect {3};  }
  endgroup
  g gi = new;
  int fails = 0;
  bit seen[4][4]; bit sa[4], sb[4];
  function automatic bit in_w(int x, int y);  return x + y > 3; endfunction
  function automatic bit in_w2(int x, int y); return y < 2 && x == 3; endfunction
  task s(input [1:0] x, input [1:0] y, input e);
    int c, t, mc;
    bit wh, w2h;
    real mx, ma, mb, got;
    string tag;
    a = x; b = y; en = e; gi.sample();
    seen[x][y] = 1; sa[x] = 1; sb[y] = 1;
    // model: user bins w, w2 (each hit when some seen product is selected) plus unselected products
    mc = 0;
    begin
      wh = 0; w2h = 0;
      for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) if (seen[i][j]) begin
        if (in_w(i, j)) wh = 1;
        if (in_w2(i, j)) w2h = 1;
        if (!in_w(i, j) && !in_w2(i, j)) mc++;
      end
      mc += wh + w2h;
    end
    mx = 100.0 * mc / 11;
    ma = 100.0 * (sa[0] + sa[1] + sa[2] + sa[3]) / 4;
    mb = 100.0 * (sb[0] + sb[1] + sb[2] + sb[3]) / 4;
    tag = $sformatf("a=%0d b=%0d", x, y);
    void'(gi.cx.get_inst_coverage(c, t));
    if (c != mc || t != 11) begin
      fails++; $error("%s cx covered/total %0d/%0d, expected %0d/11", tag, c, t, mc);
    end
    got = gi.cx.get_inst_coverage();
    if (got < mx - 0.005 || got > mx + 0.005) begin fails++; $error("%s cx %0.2f, expected %0.2f", tag, got, mx); end
    got = gi.get_inst_coverage();
    if (got < (ma + mb + mx) / 3 - 0.005 || got > (ma + mb + mx) / 3 + 0.005) begin
      fails++; $error("%s total %0.2f, expected %0.2f", tag, got, (ma + mb + mx) / 3);
    end
  endtask
  initial begin
    s(0,0,0); s(0,1,1); s(1,1,1); s(2,1,1); s(2,2,0); s(3,2,1);s(1,3,1); s(3,3,1); s(0,0,1);
    if (fails == 0) $display("PASSED");
  end
endmodule
