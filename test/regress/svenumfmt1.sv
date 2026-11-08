// Enum values with %p, %s, %d, %b and the enum methods.
// Expected values: IEEE 1800-2017 6.19.5 (first, last, next, prev, num, name; next/prev
// wrap around, name() of a value that is not a member is the empty string, next() of a
// non-member gives the first/default value), 21.2.1.7 (%p assignment-pattern text: the
// layout is implementation defined, only the content is checked), 21.2.1.8 (%s prints the
// value as 8-bit ASCII like any integral value, so %s of an enum equals %s of its integer
// value), 6.19 (an enum is integral), 21.2.1 (width and justification of %s and %d).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svenumfmt1;
  typedef enum {RED, GREEN, BLUE} color_t;
  typedef enum logic [1:0] {A=0, B=1, C=3} l_t;
  color_t c, k;
  l_t l;
  int i = 5;
  logic [3:0] x = 4'b1x01;
  int err = 0;
  string s;

  function automatic string nows(string t);
    string r = "";
    foreach (t[j]) if (t[j] != " ") r = {r, string'(t[j])};
    return r;
  endfunction
  function automatic bit has(string t, string sub);
    for (int j = 0; j + sub.len() <= t.len(); j++)
      if (t.substr(j, j + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction

  initial begin
    c = GREEN;
    s = $sformatf("%p", c);
    `CHK(has(s, "GREEN"), "p-format of enum contains the label")
    `CHK($sformatf("%s", c) == $sformatf("%s", int'(c)), "s-format of enum equals s-format of its value")
    `CHK($sformatf("%-10s", c.name()) == "GREEN     ", "left-justified width 10 of name()")
    c = color_t'(7);
    `CHK($sformatf("%0d", c) == "7", "d-format of non-member")
    `CHK(c.name() == "", "name() of non-member is empty")
    `CHK(c.next() == RED, "next() of non-member")
    l = l_t'(2);
    `CHK($sformatf("%0d", l) == "2", "d-format of non-member l")
    `CHK(l.name() == "", "name() of non-member l")
    l = l_t'(2'bx1);
    `CHK($sformatf("%b", l) == "x1", "b-format of 4-state enum with x")
    k = RED;
    `CHK(k.first() == RED && k.last() == BLUE && k.next() == GREEN, "first/last/next")
    `CHK(k.name() == "RED", "name RED")
    `CHK(k.prev() == BLUE, "prev wraps")
    `CHK(k.next(2) == BLUE, "next(2)")
    `CHK(k.last().name() == "BLUE", "last().name()")
    `CHK(k.num() == 3, "num")
    `CHK(k.next().name() == "GREEN", "next().name()")
    k = BLUE;
    `CHK(k.next() == RED, "next wraps")
    `CHK(k.prev() == GREEN, "prev")
    `CHK(k.name() == "BLUE", "name BLUE")
    `CHK($sformatf("%10s", k.name()) == "      BLUE", "right justified width 10")
    // %p of an int: content only
    `CHK(nows($sformatf("%p", i)) == "5", "p-format of int")
    s = $sformatf("%p", x);
    `CHK(has(s, "x") || has(s, "X"), "p-format of vector with x shows an unknown digit")
    if (err == 0) $display("PASSED");
  end
endmodule
