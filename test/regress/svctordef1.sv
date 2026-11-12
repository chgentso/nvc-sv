// The default value of a constructor argument may use the arguments before
// it, as it can for any subroutine (IEEE 1800-2017 13.5.3).
class svctordef1_base;
  int depth, tile, rows;
  function new(int depth, int tile = depth, int rows = 1);
    this.depth = depth; this.tile = tile; this.rows = rows;
  endfunction
endclass

class svctordef1_derived extends svctordef1_base;
  function new(int depth);
    super.new(depth);
  endfunction
endclass

module svctordef1;
  initial begin
    automatic svctordef1_base a = new(16);
    automatic svctordef1_base b = new(8, 4);
    automatic svctordef1_base c = new(32, , 3);
    automatic svctordef1_derived d = new(5);
    if (a.tile != 16 || a.rows != 1) $fatal(1, "a %0d %0d", a.tile, a.rows);
    if (b.tile != 4 || b.rows != 1) $fatal(1, "b %0d %0d", b.tile, b.rows);
    if (c.tile != 32 || c.rows != 3) $fatal(1, "c %0d %0d", c.tile, c.rows);
    if (d.tile != 5 || d.depth != 5) $fatal(1, "d %0d %0d", d.tile, d.depth);
    $display("PASSED");
  end
endmodule
