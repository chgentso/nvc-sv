// Self-checking: constraints with solve-before inside foreach, and a rand associative array element.
// Expected values: IEEE 1800-2017 18.5 (constraints, foreach, solve...before), 18.4 (rand variables),
// 7.8 (associative arrays). The existing element dl[5] is a random variable and is constrained to 0.
typedef struct { int a; bit [3:0] b; } s_t;
class C;
  rand s_t arr[2];
  rand bit sel;
  rand bit dl[int];
  rand int v;
  constraint c1 { v inside {[1:5]}; }
  constraint c2 {
    foreach (arr[i]) {
      solve sel before arr[i].a;
      arr[i].b < 8;
    }
  }
  constraint c3 { foreach (dl[k]) dl[k] == 0; }
endclass
module svcrvskip1;
  C c;
  initial begin
    c = new;
    c.dl[5] = 1;
    if (!c.randomize()) $error("randomize failed");
    else begin
      if (!(c.v inside {[1:5]})) $error("v=%0d outside [1:5]", c.v);
      else if (c.dl[5] !== 0) $error("dl[5]=%0d, expected 0", c.dl[5]);
      else if (c.dl.size() != 1) $error("dl size changed to %0d", c.dl.size());
      else if (c.arr[0].b >= 8 || c.arr[1].b >= 8) $error("arr[i].b >= 8");
      else $display("PASSED");
    end
  end
endmodule
