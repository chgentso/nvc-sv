module svtaggedarr2;
  typedef union tagged {
    int i;
    int arr[2];
    struct packed {logic [3:0] a; logic [3:0] b;} p;
  } u_t;
  typedef union tagged { void None; int arr[2][2]; } w_t;
  u_t u, v;
  w_t w;
  initial begin
    u = tagged arr '{1, 2};
    v = u;
    $display("%0d %0d", v.arr[0], v.arr[1]);
    v.arr[0] = 7;
    $display("%0d %0d", u.arr[0], v.arr[0]);
    u = tagged i 5;
    $display("%0d", u.i);
    w = tagged arr '{'{1, 2}, '{3, 4}};
    $display("%0d %0d", w.arr[0][1], w.arr[1][0]);
    w = tagged None;
    $display("%p", w);
    w = tagged arr '{'{1, 2}, '{3, 4}};
    $display("%p", w);
    v = tagged arr '{3, 4};
    $display("%p", v);
    u = tagged p 8'h5a;
    $display("%p", u);
    v = tagged arr '{default: 9};
    $display("%0d %0d", v.arr[0], v.arr[1]);
  end
endmodule
