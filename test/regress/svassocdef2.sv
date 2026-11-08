// A default value is used by compound assignments
// Self-checking. Expected values: IEEE 1800-2017 7.9.11 (default: in an associative array literal),
// 7.8.6 (a read of a nonexistent entry gives the default; a compound assignment or ++ reads the default,
// then writes the entry), 7.9.1 (num), 7.9.2 (delete(key); the default still applies to the deleted key),
// 5.11. Left out (the LRM does not say whether the default is copied by whole-array assignment): cc["q"] after
// cc = aa and bb["q"] after bb = cc.
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
module svassocdef2;
  int errs = 0;
  int aa [string] = '{default:9};
  int bb [string];
  int cc [string];
  initial begin
    aa["x"] += 1;
    `CHK("A x", aa["x"], 10) `CHK("A num", aa.num(), 1)
    aa["y"]++;
    `CHK("B y", aa["y"], 10) `CHK("B num", aa.num(), 2)
    bb = '{default:3, "a":1};
    cc = aa;
    cc["z"] = 4;
    `CHK("C bb[q]", bb["q"], 3) `CHK("C aa.num", aa.num(), 2) `CHK("C cc.num", cc.num(), 3)
    `CHK("C cc[x]", cc["x"], 10) `CHK("C cc[z]", cc["z"], 4)
    bb = cc;
    `CHK("D bb.num", bb.num(), 3) `CHK("D bb[y]", bb["y"], 10)
    aa.delete("x");
    `CHK("E x", aa["x"], 9) `CHK("E num", aa.num(), 1)
    if (errs == 0) $display("PASSED");
  end
endmodule
