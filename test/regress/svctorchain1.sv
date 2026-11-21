module svctorchain1;
  class Base;
    int a;
    function new(int x);
      a = x;
    endfunction
  endclass

  class Mid extends Base(42);
    int b = 7;
  endclass

  class Leaf extends Mid;
    int c = 9;
  endclass

  initial begin
    Mid m = new;
    Leaf l = new;
    if (m.a !== 42 || m.b !== 7) $fatal(1, "mid mismatch");
    if (l.a !== 42 || l.b !== 7 || l.c !== 9) $fatal(1, "leaf mismatch");
    $display("PASSED");
  end
endmodule
