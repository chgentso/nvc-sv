// Self-checking. Expected values: IEEE 1800-2017 7.5.1 (new[n](src): the new
// array has n elements, initialised from src), 10.9.1 (array assignment
// patterns, the default: key gives a value to every element not otherwise
// given; the type of the pattern is that of the array being created).
module svdynnewpat1;
  typedef enum {RED, GREEN, BLUE} col_t;
  class C; int v; endclass
  col_t cols[];
  int ia[];
  C hs[];
  int errs = 0;
  initial begin
    cols = new[3]('{default: GREEN});
    if (cols.size() != 3) begin errs++; $error("cols.size = %0d", cols.size()); end
    foreach (cols[i]) if (cols[i] != GREEN) begin errs++; $error("cols[%0d]", i); end
    ia = new[4]('{1, 2, 3, 4});
    if (ia.size() != 4 || ia[0] != 1 || ia[3] != 4) begin errs++; $error("ia"); end
    hs = new[2]('{default: null});
    if (hs.size() != 2 || hs[0] != null || hs[1] != null) begin errs++; $error("hs"); end
    ia = new[6](ia);
    if (ia.size() != 6 || ia[3] != 4 || ia[5] != 0) begin errs++; $error("ia grow"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
