// Expected values: IEEE 1800-2017 5.7.1 (unbased unsized literals '0 '1 'x 'z fill the context width),
// 10.9.1 (assignment patterns), 6.24.1 (casting), 13.5 (argument passing), 6.20.2 (parameters)
module svunbased1;
  typedef struct packed { logic [3:0] x; logic [3:0] y; } s_t;
  typedef struct { logic [3:0] x; logic [5:0] y; } u_t;
  typedef logic [11:0] w12;
  localparam s_t P = '{x:'1, y:'0};
  localparam logic [7:0] LP = '1;
  s_t s = '{x:'1, y:'0};
  s_t s2 = '{'1, '0};
  s_t s3 = '{default:'1};
  s_t s4 = '{x:'z, y:'x};
  u_t u = '{x:'1, y:'0};
  u_t u2 = '{'1, '1};
  u_t u3 = '{default:'1};
  logic [7:0] g;
  logic [15:0] e;
  int i;
  w12 t12;
  logic [3:0] arr [0:1] = '{default:'1};
  int errs = 0;
  function automatic s_t mk(); return '{x:'0, y:'1}; endfunction
  function automatic logic [7:0] id8(logic [7:0] a); return a; endfunction
  initial begin
    if (s !== 8'b11110000) begin errs++; $error("s=%b", s); end
    if (s2 !== 8'b11110000) begin errs++; $error("s2=%b", s2); end
    if (s3 !== 8'b11111111) begin errs++; $error("s3=%b", s3); end
    if (s4 !== 8'bzzzzxxxx) begin errs++; $error("s4=%b", s4); end
    if (P !== 8'b11110000) begin errs++; $error("P=%b", P); end
    if (mk() !== 8'b00001111) begin errs++; $error("mk=%b", mk()); end
    if (u.x !== 4'b1111 || u.y !== 6'b000000) begin errs++; $error("u=%b %b", u.x, u.y); end
    if (u2.x !== 4'b1111 || u2.y !== 6'b111111) begin errs++; $error("u2=%b %b", u2.x, u2.y); end
    if (u3.x !== 4'b1111 || u3.y !== 6'b111111) begin errs++; $error("u3=%b %b", u3.x, u3.y); end
    g = 8'('1);
    if (g !== 8'hff) begin errs++; $error("g=%b", g); end
    e = 16'('x);
    if (e !== 16'bx) begin errs++; $error("e(x)=%b", e); end
    e = 16'('z);
    if (e !== 16'bz) begin errs++; $error("e(z)=%b", e); end
    i = int'('1);
    if (i !== 32'hffffffff) begin errs++; $error("i=%b", i); end
    t12 = w12'('1);
    if (t12 !== 12'hfff) begin errs++; $error("t12=%b", t12); end
    g = 8'(('1));
    if (g !== 8'hff) begin errs++; $error("g2=%b", g); end
    if (LP !== 8'hff) begin errs++; $error("LP=%b", LP); end
    if (id8('1) !== 8'hff) begin errs++; $error("id8=%b", id8('1)); end
    if (arr[0] !== 4'hf || arr[1] !== 4'hf) begin errs++; $error("arr=%b %b", arr[0], arr[1]); end
    if (errs == 0) $display("PASSED");
  end
endmodule
