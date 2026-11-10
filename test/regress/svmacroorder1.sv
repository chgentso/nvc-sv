// Pins an order the LRM leaves open (IEEE 1800-2017 4.7: the order in which
// the initial procedures of a design start is not specified).  All the text a
// macro expands to has the location of its use, so an instance and an initial
// procedure from one macro compare equal; the instance, which is declared
// first in the expansion, starts first.  Testbench code relies on this, for
// example an interface that waits for an event which the following initial
// procedure of the same macro triggers.
module svmacroorder1_waiter;
  event ev;
  bit got = 0;
  initial begin
    @ev;
    got = 1;
  end
endmodule

`define MAKE(n) \
  svmacroorder1_waiter n(); \
  initial begin -> n.ev; end

module svmacroorder1;
  `MAKE(w)
  initial begin
    #1;
    if (w.got !== 1'b1) $error("the instance did not start before the initial procedure");
    else $display("PASSED");
  end
endmodule
