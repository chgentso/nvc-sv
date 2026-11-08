// A default value in an associative array literal
// Self-checking. Expected values: IEEE 1800-2017 7.9.11 (associative array assignment patterns: default:
// gives the value returned for a read of a nonexistent entry), 7.8.6 (reading a nonexistent entry returns that
// default and does not create the entry), 7.9.1 (num, exists), 7.9.2 (delete(key)), 5.11 (array literals),
// 10.9.1. Left out (the LRM does not say whether the default is copied by whole-array assignment, kept by
// delete(), or cleared by a pattern without default): the values of bb["q"] after bb = aa and after
// bb.delete(), aa["a"] after aa = '{"x":3}, and aa["a"] after aa.delete().
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got '%s' expected '%s'", tag, got, exp); end
module svassocdef1;
  int errs = 0;
  int aa [string] = '{"a":1, "b":2, default:9};
  int bb [string];
  int cc [int];
  string words [int] = '{default: "hello"};
  int k;
  string s;
  initial begin
    `CHK("A a", aa["a"], 1) `CHK("A z", aa["z"], 9) `CHK("A num", aa.num(), 2) `CHK("A exists", aa.exists("z"), 0)
    aa["z"] = 5;
    `CHK("B z", aa["z"], 5) `CHK("B y", aa["y"], 9) `CHK("B num", aa.num(), 3)
    bb = aa;
    `CHK("C num", bb.num(), 3) `CHK("C bb[b]", bb["b"], 2) `CHK("C bb[z]", bb["z"], 5)
    bb.delete();
    `CHK("D num", bb.num(), 0)
    aa.delete("z");
    `CHK("E z", aa["z"], 9) `CHK("E num", aa.num(), 2)
    aa = '{"x":3};
    `CHK("F num", aa.num(), 1) `CHK("F x", aa["x"], 3)
    cc = '{default:7};
    `CHK("G", cc[3], 7) `CHK("G num", cc.num(), 0)
    `CHKS("H 1", words[1], "hello") `CHKS("H 2", words[2], "hello")
    words[1] = "world";
    `CHKS("I 0", words[0], "hello") `CHKS("I 1", words[1], "world") `CHK("I num", words.num(), 1)
    aa = '{"k":1, default:4};
    k = aa["nokey"];
    `CHK("J", k, 4) `CHK("J num", aa.num(), 1)
    aa.delete();
    `CHK("K num", aa.num(), 0)
    if (errs == 0) $display("PASSED");
  end
endmodule
