// Expected values: IEEE 1800-2017 7.3.2 (tagged unions; packed tagged unions keep the tag in the leftmost bits and
// the member right-justified, padding bits are undefined), 11.9 (tagged union expressions), 11.4.5 (equality)
module svtagged1;
  typedef union tagged { void Invalid; int Valid; bit [3:0] Small; } u_t;
  typedef struct { int x; u_t u; } s_t;
  typedef union tagged packed { logic [2:0] a; bit [5:0] b; bit [1:0] c; } p_t;
  union tagged { void nothing; byte val; } anon;
  u_t a, b;
  s_t s;
  p_t q;
  int i;
  int errs;
  logic [7:0] raw;
  logic r1;

  function u_t make(int n);
    if (n < 0)
      return tagged Invalid;
    else
      return tagged Valid (n);
  endfunction

  initial begin
    a = tagged Valid (7);
    i = a.Valid;
    if (i !== 7) begin $error("i=%0d", i); errs++; end
    a.Valid = 9;
    if (a.Valid !== 9) begin $error("after set %0d", a.Valid); errs++; end
    b = a;
    if (a == b) r1 = 1'b1; else r1 = 1'b0;
    if (b.Valid !== 9 || r1 !== 1'b1) begin $error("copy"); errs++; end
    b = tagged Small (3);
    if (a == b) r1 = 1'b1; else r1 = 1'b0;
    if (b.Small !== 4'd3 || r1 !== 1'b0) begin $error("small eq"); errs++; end
    if (a != b) r1 = 1'b1; else r1 = 1'b0;
    if (r1 !== 1'b1) begin $error("small ne"); errs++; end
    s.u = tagged Valid (1 + 2);
    if (s.u.Valid !== 3) begin $error("nested %0d", s.u.Valid); errs++; end
    a = make(5);
    if (a.Valid !== 5) begin $error("func %0d", a.Valid); errs++; end
    anon = tagged val (8'h5a);
    if (anon.val !== 8'h5a) begin $error("anon"); errs++; end

    // 3 members: two tag bits (a=0, b=1, c=2), member bits right-justified, padding undefined
    q = tagged a (3'b1x0);
    raw = q;
    if (raw[7:6] !== 2'b00 || raw[2:0] !== 3'b1x0 || q.a !== 3'b1x0) begin $error("q.a raw=%b", raw); errs++; end
    q = tagged c (2);
    raw = q;
    if (q.c !== 2'b10 || raw[7:6] !== 2'b10 || raw[1:0] !== 2'b10) begin $error("q.c raw=%b", raw); errs++; end
    q = tagged b (6'h2a);
    raw = q;
    if (raw[7:6] !== 2'b01 || raw[5:0] !== 6'h2a || q.b !== 6'h2a || $bits(q) !== 8) begin $error("q.b raw=%b bits=%0d", raw, $bits(q)); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
