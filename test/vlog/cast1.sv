module cast1;
  class A; endclass
  class B extends A; endclass
  class C; endclass

  A a;
  B b;
  C c;
  int i;
  int q[$];

  initial begin
    $cast(b);
    i = $cast(c, a);
    $cast(i, a);
    $cast(a, i);
    $cast(5, i);
    $cast(q, q);
    i = $cast(b, a);
  end
endmodule
