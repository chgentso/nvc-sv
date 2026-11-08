// Self-checking: struct-member solve-before ordering and constraint satisfaction.
// Expected values: IEEE 1800-2017 18.5 (constraints), 18.5.10 (solve...before only shapes the
// distribution, never the solution space), 7.2.1 (packed struct). The frequency check is statistical.
typedef struct packed {
  bit ill;
  bit [3:0] ed;
  int sew;
  int lmul;
} vt_t;
class c;
  rand vt_t vt;
  rand int unsigned vl;
  rand bit b;
  constraint k {
    solve vt before vl;
    solve vt.lmul before b;
    vt.sew inside {8, 16, 32, 64};
    vl inside {[1 : 128 / vt.sew]};
    vt.lmul inside {1, 2, 4, 8};
    b -> vt.lmul < 4;
  }
endclass
module svcrvpack2;
  initial begin
    automatic c o = new;
    int cnt[int];
    automatic bit ok = 1;
    repeat (400) begin
      if (!o.randomize()) ok = 0;
      else begin
        if (!(o.vt.sew inside {8, 16, 32, 64}) || o.vl < 1 || o.vl > 128 / o.vt.sew
            || !(o.vt.lmul inside {1, 2, 4, 8}) || (o.b && o.vt.lmul >= 4)) ok = 0;
        if (!cnt.exists(o.vt.sew)) cnt[o.vt.sew] = 0;
        cnt[o.vt.sew]++;
      end
    end
    // solving vt first makes each element size about as likely
    foreach (cnt[s]) if (cnt[s] < 40) ok = 0;
    if (cnt.size() != 4) ok = 0;
    if (!ok) $error("constraint violated or sew distribution skewed (%0d distinct sew values)", cnt.size());
    else $display("PASSED");
  end
endmodule
