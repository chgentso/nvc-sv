// Self-checking test of `iff` on a cross and on cross bins (binsof ... intersect ... iff).
// Expected values: IEEE 1800-2017 19.6 (cross; `iff` on a cross gates its sampling), 19.6.1 (user-defined
// cross bins; cross products selected by no user bin remain automatic bins, ignore/illegal excluded),
// 19.5 (a bin guard that is false means the bin is not incremented), 19.8 (get_inst_coverage with
// covered/total), 19.11 (covered / total per item; covergroup = average of ca, cb, c1, c2, c3).
// Bin counts: c1 16 automatic; c2 = w (a in {1,2}: 8 tuples) + 8 automatic = 9; c3 = w (a in {2,3}) + z (b == 0)
// + the 6 tuples with a in {0,1}, b in {1,2,3} = 8 (w needs en but the cross is only sampled when !en).
module svcovxiff1;
  bit [1:0] a, b; bit en;
  covergroup g;
    ca: coverpoint a;
    cb: coverpoint b;
    c1: cross ca, cb iff (en);
    c2: cross ca, cb { bins w = binsof(ca) intersect {1,2} iff (en); }
    c3: cross ca, cb iff (!en) { bins w = binsof(ca) intersect {2,3} iff (en); bins z = binsof(cb) intersect {0}; }
  endgroup
  g gi = new;
  int fails = 0;
  // reference model
  bit seen_all[4][4], seen_en[4][4], seen_noen[4][4];
  bit seen_a[4], seen_b[4];
  bit w2_hit, z3_hit;
  function automatic int cnt2(bit m[4][4], int alo, int ahi, int blo, int bhi);
    int n = 0;
    for (int i = alo; i <= ahi; i++) for (int j = blo; j <= bhi; j++) if (m[i][j]) n++;
    return n;
  endfunction
  function automatic int cnta(bit m[4]);
    int n = 0;
    for (int i = 0; i < 4; i++) if (m[i]) n++;
    return n;
  endfunction
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  task s(input [1:0] x, input [1:0] y, input e);
    int c, t, c3cov;
    real m1, m2, m3, ma, mb;
    string tag;
    a = x; b = y; en = e; gi.sample();
    seen_all[x][y] = 1; seen_a[x] = 1; seen_b[y] = 1;
    if (e) begin seen_en[x][y] = 1; if (x == 1 || x == 2) w2_hit = 1; end
    else begin seen_noen[x][y] = 1; if (y == 0) z3_hit = 1; end
    m1 = 100.0 * cnt2(seen_en, 0, 3, 0, 3) / 16;
    m2 = 100.0 * (w2_hit + cnt2(seen_all, 0, 0, 0, 3) + cnt2(seen_all, 3, 3, 0, 3)) / 9;
    c3cov = z3_hit + cnt2(seen_noen, 0, 1, 1, 3);
    m3 = 100.0 * c3cov / 8;
    ma = 100.0 * cnta(seen_a) / 4; mb = 100.0 * cnta(seen_b) / 4;
    tag = $sformatf("a=%0d b=%0d en=%0d", x, y, e);
    chk({tag, " c1"}, gi.c1.get_inst_coverage(), m1);
    chk({tag, " c2"}, gi.c2.get_inst_coverage(), m2);
    chk({tag, " c3"}, gi.c3.get_inst_coverage(), m3);
    chk({tag, " total"}, gi.get_inst_coverage(), (ma + mb + m1 + m2 + m3) / 5);
    void'(gi.c3.get_inst_coverage(c, t));
    if (c != c3cov || t != 8) begin
      fails++; $error("%s c3 covered/total: got %0d/%0d, expected %0d/8", tag, c, t, c3cov);
    end
  endtask
  initial begin
    s(0,0,0); s(0,1,1); s(1,1,1); s(2,1,1); s(2,2,0); s(3,2,1);s(1,3,1); s(3,3,1); s(0,0,1);
    s(3,0,0); s(2,0,0);
    if (fails == 0) $display("PASSED");
  end
endmodule
