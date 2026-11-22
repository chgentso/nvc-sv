// The elements of a rand array of dynamic arrays of values wider than 64 bits are not solved (a warning says so), so a constraint
// which uses one of them sees the value it has: it used to build an element of an array the solver
// does not know and stop with a signal.
class B;
  rand int x;
  rand bit [127:0] arr[][];
endclass

module svcrvskiparr1;
  initial begin
    B b = new;
    int ok;
    b.arr = new[2];
    b.arr[1] = new[2];
    b.arr[1][1] = 2;
    repeat (10) begin
      ok = b.randomize() with { x == arr[1][1][7:0]; };
      if (ok != 1 || b.x != 2 || b.arr[1][1] != 128'd2) $error("ok %0d x %0d", ok, b.x);
    end
    $display("PASSED");
  end
endmodule
