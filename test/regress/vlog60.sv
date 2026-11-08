// Unpacked structures
typedef struct {
   int a;
   logic [7:0] b;
} inner_t;

typedef struct {
   int     n;
   inner_t items[$];
   string  name;
} outer_t;

module vlog60;
   struct { int mode; int limit; } settings;

   initial begin
      outer_t x, y;
      inner_t i;
      i = '{5, 8'h12};
      if (i.a != 5 || i.b !== 8'h12) $display("FAILED pattern");
      x.n = 1;
      x.name = "x";
      x.items.push_back(i);
      i.a = 6;
      if (x.items[0].a != 5) $display("FAILED value copy");
      y = x;
      y.items[0].a = 9;
      y.name = "y";
      if (x.items[0].a != 5 || x.name != "x") $display("FAILED deep copy");
      if (y.items[0].a != 9) $display("FAILED member write");
      settings.mode = 2;
      if (settings.mode != 2) $display("FAILED module struct");
      $display("PASSED");
   end
endmodule
