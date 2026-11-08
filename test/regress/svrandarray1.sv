// Scope randomize of an array and an array of structures.
// Expected values: IEEE 1800-2017 18.12 (scope randomize, std::randomize ... with; returns 1 on
// success), 18.4 (variables are given unconstrained random values: statistical check only)
module svrandarray1;
  typedef struct {
    rand bit [15:0] data;
    bit [3:0] aux;
  } ch_t;
  ch_t ch[4];
  int unsigned temps[3];
  int ok, differ;
  bit [7:0] x;
  int errs = 0;
  bit seen_differ_ch, seen_differ_t;
  initial begin
    // Random values: repeat a few times; equal values in every attempt have probability ~2^-48.
    repeat (5) begin
      ok = randomize(ch);
      if (ok !== 1) begin errs++; $error("randomize(ch) returned %0d", ok); end
      differ = 0;
      for (int i = 1; i < 4; i++) if (ch[i].data != ch[0].data) differ++;
      if (differ > 0) seen_differ_ch = 1;
      ok = randomize(temps);
      if (ok !== 1) begin errs++; $error("randomize(temps) returned %0d", ok); end
      if ((temps[0] != temps[1]) || (temps[1] != temps[2])) seen_differ_t = 1;
    end
    if (!seen_differ_ch) begin errs++; $error("array of structs never varied"); end
    if (!seen_differ_t) begin errs++; $error("array never varied"); end
    repeat (20) begin
      ok = std::randomize(x) with { x > 200; };
      if (ok !== 1 || !(x > 200)) begin errs++; $error("std::randomize with: ok=%0d x=%0d", ok, x); end
    end
    ok = std::randomize(ch);
    if (ok !== 1) begin errs++; $error("std::randomize(ch) returned %0d", ok); end
    if (errs == 0) $display("PASSED");
  end
endmodule
