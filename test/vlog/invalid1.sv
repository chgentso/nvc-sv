module invalid1;
  class base;
    virtual function void check(int a);
    endfunction
    function void plain(int a);
    endfunction
  endclass

  class derived extends base;
    function void check(int t, int exp, int got);
    endfunction
    function void plain(int a, int b);
    endfunction
  endclass

  class rect;
    int w, h;
    function new(int w, int h);
      this.w = w; this.h = h;
    endfunction
  endclass

  class point;
  endclass

  logic clk;
  logic [3:0] q, r;
  rect a;
  point p;

  initial begin
    a = new(3);
    a = new(3, 4);
    p = point::new(1);
    p = new;
  end

  initial q = 0;
  always_ff @(posedge clk) q <= q + 1;
  always_ff @(posedge clk) r <= r + 1;
endmodule
