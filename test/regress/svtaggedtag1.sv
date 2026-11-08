// IEEE 1800-2017 7.3.2 / 11.9: reading a member of a tagged union other than
// the member it currently holds (or before any member was set) is an error.
// nvc reports a run-time error naming both members and reads the default
// value of the member.
module svtaggedtag1;
  typedef union tagged { int i; real r; void n; } u_t;
  u_t u;
  int x;
  initial begin
    $display("A");
    x = u.i;
    $display("B %0d", x);
    u = tagged r 2.5;
    x = u.i;
    $display("C %0d", x);
    u = tagged n;
    x = u.i;
    $display("D %0d", x);
    u = tagged i 7;
    $display("E %0d %p", u.i, u);
  end
endmodule
