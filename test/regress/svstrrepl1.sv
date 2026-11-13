// Self-checking. IEEE 1800-2017 11.4.12.1, 6.16: the repetition count of a
// string literal in a string concatenation need not be constant.
module svstrrepl1;
  integer n = 3;
  string s, s2 = "ab";
  initial begin
    s = {s2, {n{"*"}}};
    if (s != "ab***" || s.len() != 5) $error("FAIL a '%s'", s);
    n = 0;
    s = {s2, {n{"*"}}};
    if (s != "ab") $error("FAIL b '%s'", s);
    n = 2;
    s = {n{"xy"}};
    if (s != "xyxy") $error("FAIL c '%s'", s);
    $display("PASSED");
  end
endmodule
