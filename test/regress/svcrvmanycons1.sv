// A class with more than 64 constraint blocks: every block applies and constraint_mode of a block
// beyond the 64th turns that block, and only that block, off (IEEE 1800-2017 18.9).
class A;
  rand bit [7:0] v;
  constraint c0 { v != 100; }
  constraint c1 { v != 101; }
  constraint c2 { v != 102; }
  constraint c3 { v != 103; }
  constraint c4 { v != 104; }
  constraint c5 { v != 105; }
  constraint c6 { v != 106; }
  constraint c7 { v != 107; }
  constraint c8 { v != 108; }
  constraint c9 { v != 109; }
  constraint c10 { v != 110; }
  constraint c11 { v != 111; }
  constraint c12 { v != 112; }
  constraint c13 { v != 113; }
  constraint c14 { v != 114; }
  constraint c15 { v != 115; }
  constraint c16 { v != 116; }
  constraint c17 { v != 117; }
  constraint c18 { v != 118; }
  constraint c19 { v != 119; }
  constraint c20 { v != 120; }
  constraint c21 { v != 121; }
  constraint c22 { v != 122; }
  constraint c23 { v != 123; }
  constraint c24 { v != 124; }
  constraint c25 { v != 125; }
  constraint c26 { v != 126; }
  constraint c27 { v != 127; }
  constraint c28 { v != 128; }
  constraint c29 { v != 129; }
  constraint c30 { v != 130; }
  constraint c31 { v != 131; }
  constraint c32 { v != 132; }
  constraint c33 { v != 133; }
  constraint c34 { v != 134; }
  constraint c35 { v != 135; }
  constraint c36 { v != 136; }
  constraint c37 { v != 137; }
  constraint c38 { v != 138; }
  constraint c39 { v != 139; }
  constraint c40 { v != 140; }
  constraint c41 { v != 141; }
  constraint c42 { v != 142; }
  constraint c43 { v != 143; }
  constraint c44 { v != 144; }
  constraint c45 { v != 145; }
  constraint c46 { v != 146; }
  constraint c47 { v != 147; }
  constraint c48 { v != 148; }
  constraint c49 { v != 149; }
  constraint c50 { v != 150; }
  constraint c51 { v != 151; }
  constraint c52 { v != 152; }
  constraint c53 { v != 153; }
  constraint c54 { v != 154; }
  constraint c55 { v != 155; }
  constraint c56 { v != 156; }
  constraint c57 { v != 157; }
  constraint c58 { v != 158; }
  constraint c59 { v != 159; }
  constraint c60 { v != 160; }
  constraint c61 { v != 161; }
  constraint c62 { v != 162; }
  constraint c63 { v != 163; }
  constraint c64 { v != 164; }
  constraint c65 { v != 165; }
  constraint c66 { v != 166; }
  constraint c67 { v != 167; }
  constraint c68 { v != 168; }
  constraint c69 { v != 169; }
endclass
class B extends A;
  constraint d { v != 200; }
endclass
module svcrvmanycons1;
  A a;
  B b;
  int fails = 0, r;
  bit seen169, seen103;
  initial begin
    a = new; b = new;
    repeat (500) begin
      r = a.randomize();
      if (!r || (a.v >= 100 && a.v < 170)) begin fails++; $error("all constraints: %0d", a.v); end
      r = b.randomize();
      if (!r || (b.v >= 100 && b.v < 170) || b.v == 200) begin fails++; $error("derived: %0d", b.v); end
    end
    a.c69.constraint_mode(0);
    a.c3.constraint_mode(0);
    if (a.c69.constraint_mode() != 0 || a.c3.constraint_mode() != 0 || a.c68.constraint_mode() != 1) begin
      fails++; $error("constraint_mode query");
    end
    repeat (4000) begin
      r = a.randomize();
      if (!r || (a.v >= 100 && a.v < 169 && a.v != 103)) begin fails++; $error("c3 and c69 off: %0d", a.v); end
      if (a.v == 169) seen169 = 1;
      if (a.v == 103) seen103 = 1;
    end
    if (!seen169 || !seen103) begin fails++; $error("disabled constraints did not let their values through"); end
    a.constraint_mode(1);
    repeat (500) begin
      r = a.randomize();
      if (!r || (a.v >= 100 && a.v < 170)) begin fails++; $error("all on again: %0d", a.v); end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
