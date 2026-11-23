// An argument of std::randomize which is a member of a member, an element
// of an object in an object, a member of a structure, or an element with a
// variable index is found in the with block by its name
typedef struct { int x; } st_t;
class inner;
  logic [3:0] c;
  logic [3:0] d[2];
  logic [3:0] dyn[];
  st_t s;
endclass
class outer;
  inner b;
endclass

module svcrvstdexpr1;
  int idx = 1;
  outer o;
  int bad;
  initial begin
    o = new;
    o.b = new;
    o.b.dyn = new[3];
    for (int n = 0; n < 10; n++) begin
      if (!std::randomize(o.b.c) with { o.b.c inside {[5:9]}; }) bad++;
      if (o.b.c < 5 || o.b.c > 9) bad++;
      if (!std::randomize(o.b.d[1]) with { o.b.d[1] inside {[2:6]}; }) bad++;
      if (o.b.d[1] < 2 || o.b.d[1] > 6) bad++;
      if (!std::randomize(o.b.dyn[idx]) with { o.b.dyn[idx] inside {[1:3]}; })
        bad++;
      if (o.b.dyn[idx] < 1 || o.b.dyn[idx] > 3) bad++;
      if (!std::randomize(o.b.s.x) with { o.b.s.x inside {[7:9]}; }) bad++;
      if (o.b.s.x < 7 || o.b.s.x > 9) bad++;
      if (!std::randomize(o.b.c, o.b.s.x) with { o.b.c == 3; o.b.s.x == o.b.c + 1; })
        bad++;
      if (o.b.c != 3 || o.b.s.x != 4) bad++;
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
