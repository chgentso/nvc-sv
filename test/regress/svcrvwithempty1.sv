// randomize() with () { ... } binds no name into the class: a name which is also a property of the
// object is the one of the caller (IEEE 1800-2017 18.7.1)
class Cls;
  rand int m_x;
  int lo = -100;
endclass
function int func_empty(Cls obj, int lo, int hi);
  return obj.randomize() with () { obj.m_x > lo; obj.m_x < hi; };
endfunction
module svcrvwithempty1;
  Cls c;
  int fails = 0;
  initial begin
    c = new;
    repeat (20) begin
      if (!func_empty(c, 5, 9) || c.m_x <= 5 || c.m_x >= 9) begin fails++; $error("m_x=%0d", c.m_x); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
