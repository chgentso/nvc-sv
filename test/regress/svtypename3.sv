// IEEE 1800-2017 20.6.1: $typename returns a string, so its result compares
// and concatenates like any other string value (6.16, 11.4.5, 11.4.12.2).
module svtypename3;
  int a; int unsigned b; byte c; int q[$];
  string s;
  int errs;
  initial begin
    if (!($typename(a) != $typename(b))) begin $error("!= of different types"); errs++; end
    if (!($typename(a) == $typename(a))) begin $error("== of the same type"); errs++; end
    if ($typename(a) != "int") begin $error("== with a literal"); errs++; end
    if (!($typename(a) < $typename(b))) begin $error("< of strings"); errs++; end
    s = {"<", $typename(c), ">"};
    if (s != "<byte>") begin $error("concatenation gave \"%s\"", s); errs++; end
    s = $sformatf("%s", {"<", $typename(b), ">"});
    if (s != "<int unsigned>") begin $error("concatenation in an argument gave \"%s\"", s); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
