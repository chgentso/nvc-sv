// IEEE 1800-2017 13.3: a task cannot be called in an expression, only as a
// statement.
module taskexpr1;
  class C;
    task t;
    endtask
    function int f;
      return 1;
    endfunction
    task run;
      t();                                      // OK
      this.t();                                 // OK
    endtask
  endclass
  C c;
  int x;
  initial begin
    c = new;
    x = c.t();                                  // Error
    x = c.f();                                  // OK
    c.t();                                      // OK
    c.t;                                        // OK
    void'(c.f());                               // OK
  end
endmodule
