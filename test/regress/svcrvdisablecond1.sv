// disable soft under a condition which no random variable changes is decided when the constraints are built
class DisSoft;
  bit override_flag;
  rand bit [3:0] x;
  constraint c_soft_x { soft x == 4'h5; }
  constraint c_override {
    (override_flag == 1'b1) -> disable soft x ;
    (override_flag == 1'b1) -> x == 4'hc ;
  }
endclass
class Split;
  bit [1:0] pick;
  rand bit [3:0] a, b;
  constraint c_soft_a { soft a == 4'h7; }
  constraint c_soft_b { soft b == 4'h8; }
  constraint c_split {
    if (pick == 2'd0) { disable soft a; a == 4'h1; }
    else              { disable soft b; b == 4'h2; }
  }
endclass
module svcrvdisablecond1;
  initial begin
    DisSoft d = new; Split s = new; int ok;
    ok = d.randomize(); if (ok != 1 || d.x != 5) $error("flag0 %0d %0d", ok, d.x);
    d.override_flag = 1; ok = d.randomize(); if (ok != 1 || d.x != 12) $error("flag1 %0d %0d", ok, d.x);
    ok = s.randomize(); if (ok != 1 || s.a != 1 || s.b != 8) $error("pick0 %0d %0d %0d", ok, s.a, s.b);
    s.pick = 1; ok = s.randomize(); if (ok != 1 || s.a != 7 || s.b != 2) $error("pick1 %0d %0d %0d", ok, s.a, s.b); else $display("PASSED");
  end
endmodule
