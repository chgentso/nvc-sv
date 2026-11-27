// Equality of unpacked structures with string members (IEEE 1800-2017
// 11.4.5): the strings compare by their characters
module svstructcmp3;
  typedef struct { int k; string n; } rec_t;
  typedef struct { rec_t r; string tag; } outer_t;

  rec_t   r1, r2;
  outer_t o1, o2;

  initial begin
    r1 = '{1, "x"}; r2 = '{1, "x"};
    $display("%0d %0d %0d", r1 == r2, r1 != r2, r1 === r2);
    r2.n = "y";
    $display("%0d %0d %0d", r1 == r2, r1 != r2, r1 !== r2);
    r2.n = "xx";
    $display("%0d", r1 == r2);
    o1 = '{'{2, "a"}, "t"}; o2 = o1;
    $display("%0d", o1 == o2);
    o2.r.n = "";
    $display("%0d", o1 == o2);
  end
endmodule
