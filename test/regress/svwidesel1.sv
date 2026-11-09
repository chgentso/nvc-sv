// Selecting bits and parts of a wide packed array variable with a run-time
// index in a class member and in a structure field: in range, partly out
// of range, x index and a negative index (IEEE 1800-2017 11.5.1, 11.5.2).

module svwidesel1;
   typedef struct {
      bit [2:0][255:0] w;
      logic [15:0][63:0] l;
   } st_t;

   class C;
      logic [15:0][63:0] mem;
      bit [7:0][255:0] b;
   endclass

   function automatic int sum(st_t s);
      int n = 0;
      for (int i = 0; i < 3; i++)
         n += s.w[i][7:0];
      return n;
   endfunction

   initial begin
      automatic C c = new;
      st_t s;
      int i;
      logic [3:0] xi;
      logic [63:0] e;

      for (i = 0; i < 16; i++) c.mem[i] = 64'h1000 + i;
      for (i = 0; i < 8; i++) c.b[i] = 256'(i + 100);
      for (i = 0; i < 16; i++) begin
         if (c.mem[i] !== 64'h1000 + i) $fatal(1, "mem[%0d] = %h", i, c.mem[i]);
      end
      for (i = 0; i < 8; i++) begin
         if (c.b[i] !== 256'(i + 100)) $fatal(1, "b[%0d]", i);
      end

      // Out of range reads give x for four-state and 0 for two-state
      i = 16;
      e = c.mem[i];
      if (e !== 64'bx) $fatal(1, "mem[16] = %h", e);
      i = -1;
      e = c.mem[i];
      if (e !== 64'bx) $fatal(1, "mem[-1] = %h", e);
      xi = 4'bx;
      e = c.mem[xi];
      if (e !== 64'bx) $fatal(1, "mem[x] = %h", e);
      i = 8;
      if (c.b[i] !== 256'd0) $fatal(1, "b[8] = %h", c.b[i]);

      for (i = 0; i < 3; i++) s.w[i] = 256'(i + 5);
      if (sum(s) != 5 + 6 + 7) $fatal(1, "sum");
      for (i = 0; i < 16; i++) s.l[i] = {32'(i), 32'hdead};
      for (i = 0; i < 16; i++) begin
         if (s.l[i] !== {32'(i), 32'hdead}) $fatal(1, "l[%0d]", i);
      end
      i = 20;
      if (s.l[i] !== 64'bx) $fatal(1, "l[20]");
      $display("PASSED");
   end
endmodule
