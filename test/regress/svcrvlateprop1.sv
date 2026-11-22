// A constraint can use properties which are declared after it in the class (IEEE 1800-2017 18.5)
typedef enum { ONE = 1, TWO = 2, THREE = 3 } num_t;

class Cls;
  constraint A { v inside {ONE, THREE}; }
  constraint B { w == 5; x inside {1, 2} || x inside {4, 5}; }
  constraint C { z < 3 * 7; z > 5 + 8; }

  rand num_t v;
  rand logic [63:0] w;
  rand logic [47:0] x;
  rand logic [22:0] z;
endclass

module svcrvlateprop1;
  Cls c;
  int fails = 0;
  initial begin
    c = new;
    repeat (30) begin
      if (!c.randomize() || (c.v != ONE && c.v != THREE) || c.w != 5 || !(c.x inside {1, 2, 4, 5})
          || c.z <= 13 || c.z >= 21) begin
        fails++; $error("v=%0d w=%0d x=%0d z=%0d", c.v, c.w, c.x, c.z);
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
