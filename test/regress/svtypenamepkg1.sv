// $typename keeps the case of the package that declares a class
// IEEE 1800-2017 20.6.1: a class type is named with the package or enclosing class that
// declares it followed by ::; 5.6: identifiers are case sensitive.
package SvTnPkg1;
  class Cls;
    class Inner; endclass
  endclass
  typedef Cls::Inner inner_t;
endpackage
module svtypenamepkg1;
  int errs = 0;
  function automatic void check(string got, string exp);
    if (got != exp) begin
      $display("got '%s' expected '%s'", got, exp); errs++;
    end
  endfunction
  SvTnPkg1::Cls c;
  SvTnPkg1::inner_t i;
  initial begin
    check($typename(c), "class SvTnPkg1::Cls");
    check($typename(SvTnPkg1::Cls), "class SvTnPkg1::Cls");
    check($typename(i), "class SvTnPkg1::Cls::Inner");
    if (errs == 0) $display("PASSED");
  end
endmodule
