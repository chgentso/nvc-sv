// Associative arrays with 128-bit and packed-struct keys (self-checking).
// Expected values: IEEE 1800-2017 7.8.4 (integral index: ordered numerically, unsigned for bit types),
// 7.9.1-7.9.7 (num, exists, delete, first/next/last/prev), 12.7.3 (foreach), 7.2.1 (packed struct is an
// unsigned vector with the first member most significant)
typedef struct packed { bit [63:0] lo; bit [63:0] hi; int unsigned s; } rng_t;
module svassocwide1;
  int a[bit [127:0]];
  string b[rng_t];
  bit [127:0] k;
  rng_t r, r2;
  int errs = 0;
  int n;
  bit [127:0] exp_keys [4];
  int exp_vals [4];

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    exp_keys[0] = 128'h5;
    exp_keys[1] = 128'h1_0000_0000_0000_0000;
    exp_keys[2] = 128'h2_0000_0000_0000_0000;
    exp_keys[3] = 128'hffff_0000_0000_0003_0000_0000_0000_0000;
    exp_vals[0] = 2; exp_vals[1] = 1; exp_vals[2] = 4; exp_vals[3] = 3;
    a[128'h1_0000_0000_0000_0000] = 1;
    a[128'h5] = 2;
    a[128'hffff_0000_0000_0003_0000_0000_0000_0000] = 3;
    a[128'h2_0000_0000_0000_0000] = 4;
    ck(a.num() == 4, "num");
    ck(a.exists(128'h5) && !a.exists(128'h6), "exists");
    // ascending numeric traversal
    n = 0;
    if (a.first(k)) do begin
      ck(n < 4 && k == exp_keys[n] && a[k] == exp_vals[n], "ascending traversal");
      n++;
    end while (a.next(k));
    ck(n == 4, "ascending count");
    // descending traversal
    n = 0;
    if (a.last(k)) do begin
      ck(n < 4 && k == exp_keys[3-n] && a[k] == exp_vals[3-n], "descending traversal");
      n++;
    end while (a.prev(k));
    ck(n == 4, "descending count");
    a.delete(128'h5);
    n = 1;
    foreach (a[i]) begin
      ck(n < 4 && i == exp_keys[n] && a[i] == exp_vals[n], "foreach after delete");
      n++;
    end
    ck(n == 4, "foreach count");

    r.lo = 5; r.hi = 9; r.s = 4; b[r] = "x";
    r2.lo = 5; r2.hi = 7; r2.s = 4; b[r2] = "y";
    r2.lo = 6; b[r2] = "z";
    ck(b.num() == 3, "struct-key num");
    ck(b[r] == "x" && b[r2] == "z" && b.exists(r), "struct-key values");
    // packed struct is ordered by its value: lo is most significant, then hi, then s
    n = 0;
    if (b.first(r)) do begin
      case (n)
        0: ck(r.lo == 5 && r.hi == 7 && r.s == 4 && b[r] == "y", "struct order 0");
        1: ck(r.lo == 5 && r.hi == 9 && r.s == 4 && b[r] == "x", "struct order 1");
        2: ck(r.lo == 6 && r.hi == 7 && r.s == 4 && b[r] == "z", "struct order 2");
        default: ck(0, "struct extra key");
      endcase
      n++;
    end while (b.next(r));
    ck(n == 3, "struct traversal count");
    if (errs == 0) $display("PASSED");
  end
endmodule
