// Self-checking. Expected values: IEEE 1800-2017 13.5.3 (a default argument
// value is evaluated at the call in the scope of the subroutine declaration,
// so it may use a property, here through a handle, of the object), 9.3.2 (a
// process forked from a method can use the properties of the object), 8.15
// (inherited properties) and 8.25 (parameterized base class).
class seqr_c; int h = 40; endclass

class base_c #(type S = seqr_c);
  S sq;
  extern task acc(input int x, output int r, input int y = sq.h);
endclass

task base_c::acc(input int x, output int r, input int y = sq.h);
  r = x + y;
endtask

class der_c extends base_c #(seqr_c);
  int result = 0;
  extern task run();
endclass

task der_c::run();
  fork
    begin
      acc(2, result);
    end
  join
endtask

module svforkprop1;
  der_c d = new;
  initial begin
    d.sq = new;
    d.run();
    if (d.result == 42) $display("PASSED");
    else $error("result = %0d expected 42", d.result);
  end
endmodule
