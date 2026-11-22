package svexternimport1_pkg;
  parameter int MyWidth = 4;
endpackage

module svexternimport1;
  class item_c;
    import svexternimport1_pkg::*;
    rand bit [MyWidth-1:0] x;
    extern constraint x_c;
    extern function int get_limit();
  endclass

  constraint item_c::x_c {
    x inside {[1 : (1 << MyWidth) - 1]};
  }

  function int item_c::get_limit();
    return 1 << MyWidth;
  endfunction

  initial begin
    automatic item_c it = new();
    if (it.get_limit() !== 16)
      $fatal(1, "limit=%0d exp=16", it.get_limit());
    for (int i = 0; i < 8; i++) begin
      if (!it.randomize())
        $fatal(1, "randomize failed");
      if (it.x < 1 || it.x > 15)
        $fatal(1, "x=%0d out of range", it.x);
    end
    $display("PASSED");
    $finish;
  end
endmodule
