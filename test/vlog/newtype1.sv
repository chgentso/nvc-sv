module newtype1;
  class Other; endclass
  class C; endclass
  typedef C C_t;
  C c, d;
  initial begin
    c = new Other;                      // Error
    d = new C_t;                        // Error
    d = new c;                          // OK
  end
endmodule
