// Reads of nonexistent associative-array entries and read-modify-write on a missing key (self-checking).
// Expected values: IEEE 1800-2017 7.8.6 (reading a nonexistent entry returns the default value of the element
// type and does not allocate; a warning is issued), 7.8.7 / 7.9 (exists), 11.4.1 (compound assignment: the
// element is allocated with its default value before the operation, so `aa["b"] += 2` yields 2)
// (The warnings themselves go to the diagnostic stream and are not examined here.)
module svassocwarn1;
  int aa [string]; int ai [int]; bit [3:0] ab [bit [7:0]];
  typedef struct {int x;} st_t; st_t as [int];
  string sa [int];
  int errs = 0;
  int v;
  st_t sv;
  string sx;
  bit [3:0] bv;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    aa["a"] = 1;
    ck(aa["a"] == 1, "existing entry");
    v = aa["zz"]; ck(v == 0, "missing string key reads 0");
    v = ai[5];    ck(v == 0, "missing int key reads 0");
    bv = ab[8'h3]; ck(bv === 4'b0000, "missing 2-state element reads 0");
    sv = as[3];   ck(sv.x == 0, "missing struct element reads default");
    sx = sa[7];   ck(sx == "" && sx.len() == 0, "missing string element reads empty string");
    ck(!aa.exists("q"), "exists on missing key");
    ck(aa.size() == 1, "reads do not allocate");
    ck(ai.size() == 0 && ab.size() == 0 && as.size() == 0 && sa.size() == 0, "reads do not allocate (others)");
    aa["b"] += 2;
    ck(aa["b"] == 2, "+= on a missing key starts from the default value");
    ck(aa.size() == 2, "+= allocates the entry");
    if (errs == 0) $display("PASSED");
  end
endmodule
