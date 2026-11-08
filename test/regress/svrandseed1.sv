class c;
  rand int unsigned v;
endclass
module svrandseed1;
  int unsigned a, b, r;
  c o = new;
  initial begin
    a = $urandom; b = $urandom_range(100, 10);
    void'(o.randomize());
    r = o.v;
    $display("%0d %0d %0d", a, b, r);
  end
endmodule
