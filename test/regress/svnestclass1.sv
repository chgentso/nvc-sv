// Nested classes: static members reached through the enclosing class scope, from a package, and from a module
// Expected values: IEEE 1800-2017 8.23 (scope resolution operator ::), 8.23 and 8.24 (classes declared inside classes), 8.9 (static
// properties and methods are shared by all objects and reachable through a handle), 26.3 (package scope)
package pk;
  class POuter;
    static int os = 5;
    class PInner;
      static int is = 7;
      static function int get(); return is + os; endfunction
    endclass
  endclass
endpackage
class Outer;
  static int os = 5;
  int oi;
  class Inner;
    static int is = 7;
    static string nm = "inner";
    int x;
    static function int get(); return is; endfunction
    function void inc(); is++; endfunction
    class Deep;
      static int ds = 3;
    endclass
  endclass
  Inner in;
  function new(); in = new; endfunction
endclass
module svnestclass1;
  class MOuter;
    static int ms = 11;
    class MInner;
      static int mi = 13;
      static function int get(); return mi + ms; endfunction
    endclass
  endclass
  int fails = 0;
  task automatic chk(input string what, input int got, input int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", what, got, exp); fails++; end
  endtask
  initial begin
    automatic Outer o = new;
    chk("Outer::os", Outer::os, 5);
    chk("Inner::is", Outer::Inner::is, 7);
    Outer::Inner::is = 9;
    o.in.inc();
    chk("Inner::is after inc", Outer::Inner::is, 10);
    if (Outer::Inner::nm != "inner") begin $error("nm = %s", Outer::Inner::nm); fails++; end
    chk("Inner::get", Outer::Inner::get(), 10);
    chk("o.in.is", o.in.is, 10);
    chk("Deep::ds", Outer::Inner::Deep::ds, 3);
    Outer::Inner::Deep::ds++;
    chk("Deep::ds after ++", Outer::Inner::Deep::ds, 4);
    chk("pk os", pk::POuter::os, 5);
    chk("pk is", pk::POuter::PInner::is, 7);
    pk::POuter::PInner::is = 9;
    chk("pk get", pk::POuter::PInner::get(), 14);
    chk("MOuter::ms", MOuter::ms, 11);
    chk("MInner::mi", MOuter::MInner::mi, 13);
    MOuter::MInner::mi = 20;
    chk("MInner::get", MOuter::MInner::get(), 31);
    if (fails == 0) $display("PASSED");
  end
endmodule
