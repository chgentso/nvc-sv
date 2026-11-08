// %p applied to simple (non-aggregate) values.
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the value as an
// assignment pattern / in the format of the data type, integral values in
// decimal, enumeration values by name, strings, reals by value).
// The exact layout (quoting of strings, 4-state digits, real notation) is
// implementation-defined, so only the LRM-implied content is checked: the
// printed text must contain the expected decimal/name token.
typedef enum {AA, BB, CC} e_t;
typedef enum logic [1:0] {P0, P1, P2 = 3} f_t;
module svformatp1;
  int i = -5; byte by = 8'h61; bit [7:0] b = 8'hff; bit one = 1;
  logic [3:0] z = 4'b1z0z, y = 4'b10x1, x = 4'bxxxx; longint li = 64'h123456789;
  string s = "a\"b"; e_t e = CC; f_t f = P2;
  real r = 2.0, r2 = 1e10, r3 = 0.1; shortreal sr = 1.25; time tm = 15;
  int errors = 0;
  function automatic bit has(string what, string txt, string tok);
    for (int k = 0; k + tok.len() <= txt.len(); k++)
      if (txt.substr(k, k + tok.len() - 1) == tok) return 1;
    $error("%s: '%s' does not contain '%s'", what, txt, tok);
    errors++;
    return 0;
  endfunction
  string t;
  initial begin
    void'(has("int", $sformatf("%p", i), "-5"));
    void'(has("byte", $sformatf("%p", by), "97"));
    void'(has("bit[7:0]", $sformatf("%p", b), "255"));
    void'(has("bit", $sformatf("%p", one), "1"));
    t = $sformatf("%p", z);
    void'(has("logic z", t.tolower(), "z"));   // z/x digits: letter case is not checked
    void'(has("longint", $sformatf("%p", li), "4886718345"));
    t = $sformatf("%p", s);
    void'(has("string", t, "a"));
    void'(has("string", t, "b"));
    void'(has("enum", $sformatf("%p", e), "CC"));
    void'(has("enum logic", $sformatf("%p", f), "P2"));
    void'(has("real 2.0", $sformatf("%p", r), "2"));
    void'(has("real 0.1", $sformatf("%p", r3), "0.1"));
    void'(has("shortreal", $sformatf("%p", sr), "1.25"));
    void'(has("time", $sformatf("%p", tm), "15"));
    t = $sformatf("%p", x);
    void'(has("x", t.tolower(), "x"));
    t = $sformatf("%p", y);
    void'(has("y", t.tolower(), "x"));
    void'(has("i+1", $sformatf("%p", i + 1), "-4"));
    void'(has("%-6p", $sformatf("%-6p", i), "-5"));
    void'(has("%0p", $sformatf("%0p", i), "-5"));
    t = $sformatf("%p|%p|%0d", e, s, i);
    if (t.len() < 4 || t.substr(t.len() - 3, t.len() - 1) != "|-5") begin
      $error("composite format tail wrong: '%s'", t); errors++;
    end
    if ($sformatf("%d", i) != "         -5") begin $error("%%d of int: '%s'", $sformatf("%d", i)); errors++; end
    if ($sformatf("%s", s) != "a\"b") begin $error("%%s of string"); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
