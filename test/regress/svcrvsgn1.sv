// Self-checking: signedness/width of constants and system functions in constraint expressions.
// Expected values: IEEE 1800-2017 18.5 (constraints), 11.8 (signed/unsigned expression typing),
// 20.8.1 ($clog2).
parameter int XLEN = 32;
class k;
  rand int a;
  rand longint e;
  rand bit signed [7:0] s8;
  rand shortint sh;
  rand int f;
  rand int g;
  rand bit [3:0] al;
  constraint c1 { a == 32'd7; }
  constraint c2 { e == 64'd9; }
  constraint c3 { s8 == 8'd250 - 8'd240; }
  constraint c4 { sh == $clog2(300); }
  constraint c5 { f < 32'd5; }
  constraint c6 { g > -3; g < 3; g != 0; }
  constraint c7 { al == $clog2((XLEN * 4) / 8); }
endclass
module svcrvsgn1;
  initial begin
    automatic k o = new;
    automatic bit ok = 1;
    repeat (50) begin
      if (!o.randomize()) ok = 0;
      else if (o.a != 7 || o.e != 9 || o.s8 != 10 || o.sh != 9 || !(o.f >= 0 && o.f < 5)
               || !(o.g inside {-2, -1, 1, 2}) || o.al != 4) begin
        ok = 0;
        $error("BAD %0d %0d %0d %0d %0d %0d %0d", o.a, o.e, o.s8, o.sh, o.f, o.g, o.al);
      end
    end
    if (ok) $display("PASSED");
  end
endmodule
