// Self-checking, run with --relaxed. Expected values: IEEE 1800-2017 6.16
// (s[i] is a byte; a string and its characters, an integral value assigned to
// a string is converted to a string of its characters with NULs dropped),
// 6.16 (string comparison operators == != < <= > >= compare lexicographically).
module svstrcmpchar1;
  int errs = 0;
  function automatic int find_last(string target, string s);
    int index = -1;
    for (int i = 0; i < s.len(); i++) if (s[i] == target) index = i;
    return index;
  endfunction
  initial begin
    string s = "abcabc";
    byte b = "b";
    string t = "c";
    if (find_last("b", s) != 4) begin errs++; $error("last b"); end
    if (find_last("z", s) != -1) begin errs++; $error("last z"); end
    if (!(b == "b")) begin errs++; $error("b == literal"); end
    if (!(t == s[2])) begin errs++; $error("string == byte"); end
    if (s[1] != "b") begin errs++; $error("s[1]"); end
    if (!(s[0] < "b")) begin errs++; $error("a < b"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
