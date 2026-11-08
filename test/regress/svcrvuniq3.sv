// Self-checking: unique constraint over arrays, queues and scalars.
// Expected values: IEEE 1800-2017 18.5.5 (uniqueness constraints: all elements of the listed
// variables, arrays being flattened, take pairwise different values), 18.5 (constraints).
module svcrvuniq3;
  class c1;
    rand bit [3:0] d[];
    rand bit [3:0] s1, s2;
    constraint cn { d.size() inside {[2:4]}; unique {d, s1, s2}; s1 < 3; s2 < 3; }
  endclass
  class c2;
    rand bit [3:0] d[];
    rand bit [3:0] f[3];
    constraint cn { d.size() == 5; unique {d, f}; }
    constraint cx { foreach (f[i]) f[i] < 8; foreach (d[i]) d[i] < 8; }
  endclass
  class c3;
    rand bit [3:0] d[], q[$];
    rand bit [3:0] s;
    constraint cn { d.size() == 3; q.size() inside {[2:3]}; unique {d, q, s}; s < 4; }
    constraint cx { foreach (q[i]) q[i] < 8; foreach (d[i]) d[i] < 8; }
  endclass
  initial begin
    automatic c1 o = new; automatic c2 p = new; automatic c3 t = new;
    automatic int bad, nbad, r, fails = 0;
    bad = 0; nbad = 0;
    repeat (50) begin
      if (!o.randomize()) bad++;
      else begin
        for (int i = 0; i < o.d.size(); i++) begin
          if (o.d[i] == o.s1 || o.d[i] == o.s2) nbad++;
          for (int j = i + 1; j < o.d.size(); j++) if (o.d[i] == o.d[j]) nbad++;
        end
        if (o.s1 == o.s2) nbad++;
      end
    end
    if (bad != 0 || nbad != 0) $error("A bad=%0d nbad=%0d", bad, nbad);
    fails += bad + nbad;
    bad = 0; nbad = 0;
    repeat (50) begin
      if (!p.randomize()) bad++;
      else begin
        foreach (p.d[i]) begin
          foreach (p.f[j]) if (p.d[i] == p.f[j]) nbad++;
          for (int j = i + 1; j < p.d.size(); j++) if (p.d[i] == p.d[j]) nbad++;
        end
        if (p.f[0] == p.f[1] || p.f[0] == p.f[2] || p.f[1] == p.f[2]) nbad++;
      end
    end
    if (bad != 0 || nbad != 0) $error("B bad=%0d nbad=%0d", bad, nbad);
    fails += bad + nbad;
    bad = 0; nbad = 0;
    repeat (50) begin
      if (!t.randomize()) bad++;
      else begin
        foreach (t.d[i]) begin
          foreach (t.q[j]) if (t.d[i] == t.q[j]) nbad++;
          if (t.d[i] == t.s) nbad++;
          for (int j = i + 1; j < t.d.size(); j++) if (t.d[i] == t.d[j]) nbad++;
        end
        foreach (t.q[i]) begin
          if (t.q[i] == t.s) nbad++;
          for (int j = i + 1; j < t.q.size(); j++) if (t.q[i] == t.q[j]) nbad++;
        end
      end
    end
    if (bad != 0 || nbad != 0) $error("C bad=%0d nbad=%0d", bad, nbad);
    fails += bad + nbad;
    if (fails == 0) $display("PASSED");
  end
endmodule
