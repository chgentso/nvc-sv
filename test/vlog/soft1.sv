module soft1;
  class A;
    rand int b;
    constraint c1 { soft b > 1; }
    constraint c2 { disable soft b + 1; }
    constraint c3 { disable soft b; }
  endclass
endmodule
