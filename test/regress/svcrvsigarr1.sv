// A static unpacked array of variables of the module read as a table in a constraint: inside {t} and
// foreach over t (IEEE 1800-2017 18.5.3, 18.5.8.1).
class A;
  rand bit [7:0] x;
endclass

module svcrvsigarr1;
  bit [15:0] t[4];
  logic [7:0] u[3];
  A a;
  int fails = 0;
  initial begin
    a = new;
    t = '{5, 6, 7, 8};
    u = '{20, 30, 40};
    repeat (20) begin
      if (!a.randomize() with { x inside {t}; } || a.x < 5 || a.x > 8) begin
        fails++; $error("inside: x=%0d", a.x);
      end
      if (!a.randomize() with { foreach (t[i]) x != t[i]; x < 12; } || (a.x >= 5 && a.x <= 8) || a.x >= 12) begin
        fails++; $error("foreach: x=%0d", a.x);
      end
      if (!a.randomize() with { x inside {u}; } || (a.x != 20 && a.x != 30 && a.x != 40)) begin
        fails++; $error("logic table: x=%0d", a.x);
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
