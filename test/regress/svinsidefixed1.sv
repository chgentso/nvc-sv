// A fixed size array in the set of an inside expression stands for its
// elements, in an ordinary expression and in a constraint (IEEE 1800-2017
// 11.4.13 and 18.5.4).
module svinsidefixed1;
  typedef enum logic [2:0] { E0, E1, E2, E3, E4, E5 } e_t;
  parameter e_t TERM [4] = {E1, E3, E4, E5};
  localparam int LP [3] = '{10, 20, 30};
  int var_arr [3] = '{7, 8, 9};

  class c;
    rand e_t e;
    constraint k { e inside {TERM}; }
  endclass

  initial begin
    automatic e_t e;
    automatic int n = 0;
    automatic c o = new;
    automatic int seen = 0;

    for (int i = 0; i < 6; i++) begin
      e = e_t'(i);
      if (e inside {TERM}) n |= (1 << i);
    end
    if (n[5:0] !== 6'b111010) $fatal(1, "param array mask %b", n[5:0]);
    if (!(20 inside {LP}) || !(30 inside {LP}) || (25 inside {LP})) $fatal(1, "localparam array");
    if (!(8 inside {var_arr}) || (10 inside {var_arr})) $fatal(1, "variable array");
    if (!(5 inside {1, 5, LP}) || !(30 inside {1, 5, LP})) $fatal(1, "array among other items");

    for (int i = 0; i < 40; i++) begin
      if (!o.randomize()) $fatal(1, "randomize failed");
      if (!(o.e inside {TERM})) $fatal(1, "value %s not in the set", o.e.name());
      seen |= 1 << o.e;
    end
    if (seen != 6'b111010) $fatal(1, "not every element chosen: %b", seen[5:0]);
    $display("PASSED");
  end
endmodule
