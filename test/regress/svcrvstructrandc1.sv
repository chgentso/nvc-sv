// A randc member of a rand structure, also of a nested structure, cycles
// through its values
typedef struct { randc bit [2:0] c; rand bit [3:0] r; } in_t;
typedef struct { randc bit [1:0] d; rand in_t in; } out_t;

class k;
  rand out_t o;
  constraint cr { o.in.r < 5; }
endclass

module svcrvstructrandc1;
  k x;
  int bad;
  bit [7:0] seen_c, seen_d;
  initial begin
    x = new;
    for (int n = 0; n < 8; n++) begin
      if (!x.randomize()) bad++;
      if (seen_c[x.o.in.c]) bad++;
      seen_c[x.o.in.c] = 1;
      if (x.o.in.r >= 5) bad++;
    end
    if (seen_c != 8'hff) bad++;
    for (int n = 0; n < 4; n++) begin
      void'(x.randomize());
      if (seen_d[x.o.d]) bad++;
      seen_d[x.o.d] = 1;
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
