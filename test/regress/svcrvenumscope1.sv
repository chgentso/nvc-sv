// The names of an enumeration which is the type of a property of a base class are not visible in the
// derived class unless the enumeration is written in the declaration of the property: a name imported
// from a package keeps its meaning.
package p1;
  typedef enum { IDLE, BUSY } st_e;
endpackage

package p2;
  typedef enum { DONE, BUSY } other_e;
endpackage

import p2::*;

class Base;
  rand p1::st_e st;
  rand enum { LOCAL_A, LOCAL_B } own;
endclass

class Derived extends Base;
  rand other_e o;
  constraint c { o == BUSY; st == p1::BUSY; own == LOCAL_B; }
  function int value();
    return o;
  endfunction
endclass

module svcrvenumscope1;
  initial begin
    Derived d = new;
    int ok;
    ok = d.randomize();
    if (ok != 1 || d.value() != 1 || d.st != p1::BUSY || d.own != d.LOCAL_B) $error("randomize %0d %0d", ok, d.value());
    $display("PASSED");
  end
endmodule
