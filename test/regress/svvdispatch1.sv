// Self-checking. Expected values: IEEE 1800-2017 8.20 (a call of a virtual
// method calls the method of the actual class of the object), 8.25
// (parameterized classes: each specialisation is a class, also one declared
// inside another specialisation), 23.10 (a class specialisation declared in an
// instance, here an interface bound into a module, is a type in that
// instance). The dispatch of the call during the initialisation of the package
// takes place before the specialisations of the instances are used.
package svvdispatch1_pkg;
  virtual class wrapper;
    pure virtual function int id();
  endclass

  class registry #(type T = int, int N = 0) extends wrapper;
    static registry #(T, N) me;
    static function registry #(T, N) get();
      if (me == null) me = new;
      return me;
    endfunction
    virtual function int id();
      return N;
    endfunction
  endclass

  class der #(int N = 1);
    typedef registry #(der #(N), N) type_id;
  endclass

  int first_id = der #(1)::type_id::get().id();
endpackage

interface svvdispatch1_leaf(output int got);
  import svvdispatch1_pkg::*;
  typedef der #(.N(5)) d5_t;
  typedef der #(.N(7)) d7_t;
  initial begin
    wrapper w;
    w = d5_t::type_id::get();
    got = w.id();
    w = d7_t::type_id::get();
    got = got * 100 + w.id();
  end
endinterface

module svvdispatch1_mid(output int got);
endmodule

module svvdispatch1;
  import svvdispatch1_pkg::*;
  int got, errs = 0;
  svvdispatch1_mid m(.got(got));
  bind svvdispatch1_mid svvdispatch1_leaf l(.got(got));
  initial begin
    #1;
    if (first_id != 1) begin errs++; $error("first_id = %0d", first_id); end
    if (got != 507) begin errs++; $error("got = %0d expected 507", got); end
    if (errs == 0) $display("PASSED");
  end
endmodule
