// Self-checking. IEEE 1800-2017 21.2.1: the format argument of $display and
// $sformatf is a string expression; a concatenation of string literals
// (11.4.12) is one, so its conversions apply to the arguments that follow.
module svfmtconcat1;
  string s;
  initial begin
    s = $sformatf({"x=%0d ", "y=%0d"}, 1, 2);
    if (s != "x=1 y=2") $error("got [%s]", s);
    s = $sformatf({"a=%0d"}, 5);
    if (s != "a=5") $error("got [%s]", s);
    s = $sformatf({"%s-", "%0h"}, "ab", 8'hff);
    if (s != "ab-ff") $error("got [%s]", s);
    $display({"PASS", "ED"});
  end
endmodule
