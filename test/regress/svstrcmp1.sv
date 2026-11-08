// Expected values: IEEE 1800-2017 6.16 (string comparison operators compare lexicographically by byte
// value; a shorter string that is a prefix compares less; string literals convert to string), 6.16.9
// ($sformatf result is a string)
module svstrcmp1;
  string a = "abc", b = "abd", c = "abc";
  int errs = 0;
  task automatic ck(string w, bit got, bit exp);
    if (got !== exp) begin $error("%s: got %b expected %b", w, got, exp); errs++; end
  endtask
  initial begin
    ck("a<b", a < b, 1);
    ck("a<=c", a <= c, 1);
    ck("a>b", a > b, 0);
    ck("b>=c", b >= c, 1);
    ck("a<fmt abd", a < $sformatf("%s", "abd"), 1);
    ck("fmt b > a", $sformatf("%s", "b") > a, 1);
    ck("ab<a", "ab" < a, 1);
    ck("a<abcd", a < "abcd", 1);
    ck("a>=c", a >= c, 1);
    if (errs == 0) $display("PASSED");
  end
endmodule
