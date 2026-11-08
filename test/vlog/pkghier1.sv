// IEEE 1800-2017 26.2: items in a package cannot refer to the design
// hierarchy, only to names declared in the package or imported from
// another package.
package pkghier1;
  class c;
    int y;
    function int f();
      return top.x;                     // Error
    endfunction
    function int g(c other);
      return other.y + this.y;          // OK
    endfunction
  endclass
  function int h();
    return top.u.z;                     // Error
  endfunction
endpackage
