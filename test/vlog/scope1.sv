module scope1;
  class A;
    static int count;
    int x;
  endclass

  int y;
  initial begin
    y = A::missing;
    y = A::count;
    y = nopkg::value;
    y = A::count + A::other;
  end
endmodule
