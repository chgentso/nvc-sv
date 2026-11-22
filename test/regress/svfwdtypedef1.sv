package svfwdtypedef1_pkg;
  class holder #(type T = int, type C = int);
    T item;
    C cfg;
    function int check();
      return item.get_val() + cfg.offset;
    endfunction
  endclass

  typedef my_item;
  typedef my_cfg;
  typedef my_plain_t;

  typedef holder #(my_item, my_cfg) my_holder_t;

  class my_item;
    int v;
    function int get_val();
      return v;
    endfunction
  endclass

  class my_cfg;
    int offset;
  endclass

  typedef logic [7:0] my_plain_t;
endpackage

module svfwdtypedef1;
  import svfwdtypedef1_pkg::*;

  initial begin
    automatic my_holder_t h = new();
    automatic my_plain_t p = 8'h2A;
    h.item = new();
    h.cfg = new();
    h.item.v = 30;
    h.cfg.offset = 12;
    if (h.check() !== 42 || p !== 8'h2A)
      $fatal(1, "FAIL: check=%0d p=%h", h.check(), p);
    $display("PASSED");
  end
endmodule
