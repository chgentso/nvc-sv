virtual class Base;
  virtual function int base_val();
    return 1;
  endfunction
  virtual class Inner;
    pure virtual function int val();
  endclass
endclass

class Derived extends Base;
  virtual function int base_val();
    return 2;
  endfunction
  class Inner extends Base::Inner;
    virtual function int val();
      return 42;
    endfunction
  endclass
endclass

module svvirtualclass1;
  initial begin
    automatic Derived d = new;
    automatic Derived::Inner obj = new;
    if (d.base_val() !== 2) $fatal(1, "base_val=%0d", d.base_val());
    if (obj.val() !== 42) $fatal(1, "val=%0d", obj.val());
    $display("PASSED");
    $finish;
  end
endmodule
