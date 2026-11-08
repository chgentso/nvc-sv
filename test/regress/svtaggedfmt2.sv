// Expected values: IEEE 1800-2017 7.3.2 (packed tagged unions: widest member
// plus tag bits, members accessed through the tag), 11.9 (tagged union
// expressions), 21.2.1 (%d %h %b %o %x with minimum field width / '-' / '0'
// flags), 20.6.2 ($bits).
// How a tagged union is rendered by the integral format specifiers (as one
// number including the tag, or as "member:value") and the bit encoding of
// the tag are implementation-defined.  Checked instead: the width (8 + 2 tag
// bits), the member values read back through the tag, the tags of the three
// members are distinct, and each radix rendering of the union contains the
// digits of the member value in that radix, with the requested minimum
// field width honoured.
module svtaggedfmt2;
  typedef union tagged packed { bit [7:0] a; bit [3:0] b; void n; } tp_t;
  tp_t p;
  bit [9:0] vec;
  int errs;
  function automatic bit has(string str, string sub);
    for (int i = 0; i + sub.len() <= str.len(); i++) if (str.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  task automatic chk(bit c, string m); if (!c) begin $error("%s", m); errs++; end endtask
  initial begin
    bit [1:0] ta, tb, tn;
    string sd, sh, sb, so, sx, sw;
    chk($bits(p) == 10, $sformatf("$bits(tp_t) = %0d, expected 10", $bits(p)));
    p = tagged a 8'h5a; vec = p; ta = vec[9:8];
    sd = $sformatf("%d", p); sh = $sformatf("%h", p); sb = $sformatf("%b", p); so = $sformatf("%o", p); sx = $sformatf("%x", p);
    sw = $sformatf("%-6d", p);
    chk(p.a == 8'h5a, "member a of 5a");
    chk(has(sh, "5a"), {"%h of a=5a: ", sh}); chk(has(sd, "90"), {"%d of a=5a: ", sd});
    chk(has(sb, "01011010"), {"%b of a=5a: ", sb}); chk(has(so, "132"), {"%o of a=5a: ", so});
    chk(has(sx, "5a"), {"%x of a=5a: ", sx}); chk(sw.len() >= 6 && has(sw, "90"), {"%-6d of a=5a: [", sw, "]"});
    sw = $sformatf("%5d", p); chk(sw.len() >= 5 && has(sw, "90"), {"%5d of a=5a: [", sw, "]"});
    sd = $sformatf("%0d", p); chk(has(sd, "90"), {"%0d of a=5a: ", sd});
    p = tagged a 8'h05; sh = $sformatf("%h", p); sb = $sformatf("%b", p);
    chk(p.a == 8'h05, "member a of 05");
    chk(has(sh, "05"), {"%h of a=05: ", sh}); chk(has(sb, "00000101"), {"%b of a=05: ", sb});
    p = tagged b 4'h5; vec = p; tb = vec[9:8];
    sh = $sformatf("%h", p); sb = $sformatf("%b", p); so = $sformatf("%o", p);
    chk(p.b == 4'h5, "member b");
    chk(has(sh, "5"), {"%h of b=5: ", sh}); chk(has(sb, "0101"), {"%b of b=5: ", sb}); chk(has(so, "5"), {"%o of b=5: ", so});
    p = tagged n; vec = p; tn = vec[9:8];
    sd = $sformatf("[%d|%h|%b|%p]", p, p, p, p);
    chk(sd.len() > 0, "formatting a void member");
    chk(ta != tb && tb != tn && ta != tn, $sformatf("tags not distinct: %0d %0d %0d", ta, tb, tn));
    if (errs == 0) $display("PASSED");
  end
endmodule
