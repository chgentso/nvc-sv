class c;
  randc int w;
  randc bit [19:0] m;
  randc bit [31:0] u;
  randc bit [16:0] s;
  constraint cs { s < 6; }
endclass

module svrandc2;
  c obj = new;
  int seen[int];
  int n = 0;
  initial begin
    for (int i = 0; i < 3000; i++) begin
      if (!obj.randomize()) $fatal(1, "randomize failed");
      if (seen.exists(obj.w)) n++;
      seen[obj.w] = 1;
    end
    if (n != 0) $fatal(1, "repeated %0d", n);
    seen.delete();
    for (int i = 0; i < 3000; i++) begin
      void'(obj.randomize());
      if (seen.exists(obj.m)) n++;
      seen[obj.m] = 1;
    end
    if (n != 0) $fatal(1, "m repeated %0d", n);
    seen.delete();
    for (int i = 0; i < 3000; i++) begin
      void'(obj.randomize());
      if (seen.exists(obj.u)) n++;
      seen[obj.u] = 1;
    end
    if (n != 0) $fatal(1, "u repeated %0d", n);
    for (int k = 0; k < 4; k++) begin
      seen.delete();
      for (int i = 0; i < 6; i++) begin
        void'(obj.randomize());
        if (obj.s >= 6) $fatal(1, "s = %0d", obj.s);
        if (seen.exists(obj.s)) n++;
        seen[obj.s] = 1;
      end
      if (seen.size() != 6) $fatal(1, "cycle %0d has %0d values", k, seen.size());
    end
    $display("PASSED");
  end
endmodule
