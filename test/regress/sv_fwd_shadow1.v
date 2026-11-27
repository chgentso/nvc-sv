class OuterBase;
  int val = 11;
endclass

module sv_fwd_shadow1;
  typedef class OuterBase;
  typedef OuterBase BaseAlias;

  class Derived extends BaseAlias;
  endclass

  class OuterBase;
    int val = 99;
  endclass

  Derived d;

  initial begin
    d = new;
    if (d.val !== 99) $fatal(1, "expected 99, got %0d", d.val);
    $display("PASS");
  end
endmodule
