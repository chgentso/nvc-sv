// Empty queues and accesses outside dynamic arrays and queues
typedef struct { int a; string s; } item_t;

module vlog66;
   initial begin
      int q[$], d[], v;
      logic [7:0] lq[$], lv;
      string sq[$], sv;
      item_t iq[$], iv;

      v = 5;
      v = q.pop_front();
      if (v !== 0) $display("FAILED pop front %0d", v);
      v = 5;
      v = q.pop_back();
      if (v !== 0) $display("FAILED pop back %0d", v);
      if (q.size() != 0) $display("FAILED size");

      lv = lq.pop_back();
      if (lv !== 8'bx) $display("FAILED logic pop %b", lv);
      sv = sq.pop_front();
      if (sv != "") $display("FAILED string pop");
      iv = iq.pop_front();
      if (iv.a != 0 || iv.s != "") $display("FAILED struct pop");

      // Reads outside return the default value
      if (q[0] !== 0 || q[$] !== 0) $display("FAILED read empty");
      if (lq[3] !== 8'bx) $display("FAILED logic read");
      q.push_back(7);
      if (q[1] !== 0 || q[-1] !== 0) $display("FAILED read outside");
      if (q[0] !== 7 || q[$] !== 7) $display("FAILED read inside");

      // Writing at the end extends a queue but further out is ignored
      q[1] = 8;
      if (q.size() != 2 || q[1] != 8) $display("FAILED extend");
      q[5] = 9;
      if (q.size() != 2) $display("FAILED write outside %0d", q.size());
      q[-1] = 9;
      if (q[0] != 7) $display("FAILED write negative");

      // Dynamic arrays do not grow
      d = new[2];
      d[2] = 4;
      if (d.size() != 2 || d[2] !== 0) $display("FAILED dynamic");
      if (d[1] != 0) $display("FAILED dynamic init");

      $display("PASSED");
   end
endmodule
