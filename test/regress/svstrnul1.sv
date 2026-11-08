// Expected values: IEEE 1800-2017 6.16 (a NUL character is dropped when an integral value or a
// concatenation is converted to string; putc with 0 leaves the string unchanged), 6.16.1 (len),
// 11.4.12 (concatenation), 21.2.1.8 (%s of an integral value).
// Left out: what $sformatf("%s", v) produces for an embedded 0x00 byte is not fixed by the LRM text, so
// either dropping it (4 characters) or keeping it (5) is accepted for that line.
module svstrnul1;
  string s, t;
  bit [39:0] v = 40'h6162006364;
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  initial begin
    s = {"ab", 8'h00, "cd"};   ck("a", s.len(), 4);
    s = string'(v);            ck("c", s.len(), 4);
    t = {"x", s};              ck("d", t.len(), 5);
    s = $sformatf("%s", v);
    if (s.len() != 4 && s.len() != 5) begin $error("e: len %0d expected 4 or 5", s.len()); errs++; end
    s = "";
    s = {s, 8'h00, "z"};       ck("f", s.len(), 1);
    s = "ab"; s = {s, 8'h00};  ck("g", s.len(), 2);
    s = "abc"; s.putc(1, 0);   ck("h", s.len(), 3);
    if (errs == 0) $display("PASSED");
  end
endmodule
