// An operator assignment or increment of an associative array entry which
// does not exist creates it from the default value
// IEEE 1800-2017 7.8.7 (allocating associative array elements: aa[k] op= v
// and ++ on a missing entry allocate it, initialised to the default, and
// are not reads of a nonexistent entry), 7.8.6
module svassocupd1;
  int aa[int];
  int sa[string];
  int errs;
  initial begin
    aa[3] += 2;
    aa[4]++;
    ++aa[5];
    sa["x"] -= 1;
    if (aa.size() != 3 || aa[3] != 2 || aa[4] != 1 || aa[5] != 1) begin errs++; $display("FAILED aa %p", aa); end
    if (sa.size() != 1 || sa["x"] != -1) begin errs++; $display("FAILED sa %p", sa); end
    if (errs == 0) $display("PASSED");
  end
endmodule
