// An unpacked array variable initialised or assigned from a function
// which returns an array declared with a typedef (IEEE 1800-2017 13.4)
module vlogarrfunc1;
  typedef int arr_t[3];
  typedef int arr2_t[2][2];
  typedef bit [39:0] wide_t[3];
  typedef real real_t[2];
  function arr_t f(); f = '{1, 2, 3}; endfunction
  function arr_t g(int x); g = '{x, x + 1, x + 2}; endfunction
  function arr2_t h(); h = '{'{1, 2}, '{3, 4}}; endfunction
  function wide_t fw(); fw = '{40'h1_0000_0001, 40'h2, 40'hff_ffff_ffff}; endfunction
  function real_t fr(); fr = '{1.5, 2.5}; endfunction

  int a[3] = f();
  int b[2][2] = h();
  bit [39:0] w[3] = fw();
  real r[2] = fr();
  int z[3];
  int e[2][2];
  arr2_t m;

  initial begin
    automatic int c[3] = g(10);
    automatic int d[2][2] = h();
    automatic real lr[2] = fr();
    $display("%p %p %p", a, b, c);
    $display("%p %p", d, w);
    $display("%p %p", r, lr);
    z = g(20);
    $display("%p", z);
    e = h();
    $display("%p", e);
    m = h();
    e = '{'{0, 0}, '{0, 0}};
    e = m;
    $display("%p", e);
    r = fr();
    $display("%p", r);
  end
endmodule
