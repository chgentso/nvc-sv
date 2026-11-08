module crv5;
  class c;
    rand bit b1;
    randc int b2;
    randc bit [3:0] b3;
    constraint c1 { solve b1 before b2; }
    constraint c2 { soft b2 > 4; }
    constraint c3 { b3 dist { 3 := 1, 10 := 5 }; }
    constraint c4 { b1 -> b2 == 0; soft b1 == 0; }
  endclass
endmodule
