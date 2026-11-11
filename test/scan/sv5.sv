module decls;
  import unk_pkg::*;
  state_t state;
  other_t #(8) x;
  T y = 1;
  T a, b = 2, c;
  logic [7:0] z;
  T [7:0] w;
  counter u1 ( .clk(clk) );
  counter #(8) u2 ();
  counter u3 [3:0] ( .clk(clk) );
  T q = '{1, 2};
  unk_pkg::item_t it;
endmodule
