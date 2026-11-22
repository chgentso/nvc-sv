// An element of an element of an array of dynamic arrays in a constraint has the width of the
// element, not of the inner array.
typedef int int_arr_t[];

class A;
  rand int x;
endclass

class Cls;
  int_arr_t arr[];
  int q[][];
  task body();
    int ok, v;
    A a;
    a = new;
    arr = new[2];
    arr[1] = new[2];
    arr[1][1] = 123;
    q = new[2];
    q[1] = new[3];
    q[1][2] = -77;
    v = 1;
    repeat (10) begin
      ok = a.randomize() with {x == arr[1][1];};
      if (ok != 1 || a.x != 123) $error("arr %0d %0d", ok, a.x);
      ok = a.randomize() with {x == arr[v][v];};
      if (ok != 1 || a.x != 123) $error("arr[v][v] %0d %0d", ok, a.x);
      ok = a.randomize() with {x == q[1][2];};
      if (ok != 1 || a.x != -77) $error("q %0d %0d", ok, a.x);
    end
    $display("PASSED");
  endtask
endclass

module svcrvelem2d1;
  initial begin
    Cls c = new;
    c.body();
  end
endmodule
