// IEEE 1800-2017 8.4: a class handle is not an integral value, so printing
// one with an integer format such as %d is beyond the LRM; nvc warns and
// prints the serial number of the object, numbered from one in order of
// creation across all classes.  %p of a handle that refers back to an
// object uses the same number (21.2.1.7 leaves the text to the tool).
class C;
  C next;
endclass

class D;
endclass

module svhandleid1;
  C c1, c2;
  D d1;
  int n1, n2, n3, errs;
  string s, e;

  initial begin
    c1 = new;
    d1 = new;
    c2 = new;
    s = $sformatf("%0d", c1);  n1 = s.atoi();
    s = $sformatf("%0d", d1);  n2 = s.atoi();
    s = $sformatf("%0d", c2);  n3 = s.atoi();
    if (n1 <= 0 || n2 != n1 + 1 || n3 != n1 + 2) begin
      $error("serial numbers %0d %0d %0d", n1, n2, n3);
      errs++;
    end
    c1.next = c2;
    s = $sformatf("%p", c1);
    e = $sformatf("#%0d}", n3);
    if (s.len() < e.len() || s.substr(s.len() - e.len(), s.len() - 1) != e) begin
      $error("%%p gave %s", s);
      errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
