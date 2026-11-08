// Self-checking: power operator and $countones inside constraints.
// Expected values: IEEE 1800-2017 18.5 (constraints), 18.7 (inline constraints), 11.4.3 (power operator),
// 20.9 ($countones), 6.19.5.6 (enum name()).
typedef enum bit [2:0] {B1, B2, B4, B8} sz_e;
class Tx;
  rand sz_e sz;
  rand bit [3:0] n;
  rand int cnt;
  rand bit [7:0] strb [$:4];
  constraint c1 { cnt == 2**sz; n inside {[1:4]}; }
  constraint c2 { strb.size() == n; foreach (strb[i]) $countones(strb[i]) == 2**sz; }
endclass

module svcrvpow1;
  Tx t = new;
  int ok, bad;
  initial begin
    repeat (20) begin
      ok = t.randomize();
      if (!ok) bad++;
      else begin
        if (t.cnt != (1 << t.sz)) bad++;
        foreach (t.strb[i]) if ($countones(t.strb[i]) != t.cnt) bad++;
      end
    end
    if (bad != 0) $error("bad=%0d", bad);
    ok = t.randomize() with { sz == B8; };
    if (ok !== 1 || t.cnt !== 8) $error("sz==B8: ok=%0d cnt=%0d, expected 1 and 8", ok, t.cnt);
    ok = t.randomize() with { 2**sz == 2; };
    if (ok !== 1 || t.sz !== B2 || t.sz.name() != "B2")
      $error("2**sz==2: ok=%0d sz=%s, expected 1 and B2", ok, t.sz.name());
    if (bad == 0 && ok === 1 && t.sz === B2 && t.cnt === 2) $display("PASSED");
  end
endmodule
