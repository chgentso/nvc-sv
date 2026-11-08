// IEEE 1800-2017 8.4: accessing a member through a null handle is
// illegal.  nvc stops with a fatal error which names the class and the
// method.
module svnullcall2;
  class B;
    function void m();
    endfunction
  endclass
  B b;
  initial b.m();
endmodule
