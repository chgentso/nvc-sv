module svtypeop2;
  typedef enum {A, B = 3, C} e_t;
  enum {X, Y, Z} anon;
  logic [7:0] vec;
  class P;
    e_t pe;
  endclass
  P p = new;
  initial begin
    typedef type(anon) a_t;
    typedef type(p.pe) pe_t;
    typedef type(vec) v_t;
    a_t q;
    pe_t r;
    v_t w;
    q = q.first();
    do begin
      $display("%0d %s", q, q.name());
      q = q.next();
    end while (q != q.first());
    r = r.last();
    $display("%0d", r);
    w = '1;
    $display("%0d %0d", $bits(w), w);
  end
endmodule
