module svpatnest1;
  typedef struct packed {
    logic [3:0] a;
    logic [3:0] b;
  } ab_t;

  typedef struct packed {
    ab_t p;
    ab_t q;
  } pq_t;

  typedef struct packed {
    pq_t n;
    logic [1:0][3:0] pa;
    logic [7:0] v;
  } outer_t;

  typedef struct packed {
    ab_t [1:0] pa;
    logic [3:0] z;
  } arr_s_t;

  logic [3:0][7:0] pa;
  pq_t pq;
  outer_t o;
  arr_s_t s;
  int ua2[2][2];
  int ua3[2][2][2];
  int ud[2][3];
  logic [7:0] w[2];

  initial begin
    pa = '{default: '1};
    if (pa !== 32'hffffffff) $fatal(1, "pa=%h", pa);

    pq = '{default: 4'h3};
    if (pq !== 16'h3333) $fatal(1, "pq=%h", pq);

    pq = '{q: '{default: 4'h1}, default: 4'h2};
    if (pq !== 16'h2211) $fatal(1, "pq2=%h", pq);

    o = '{default: 1'b1};
    if (o.n !== 16'h1111 || o.pa !== 8'h11 || o.v !== 8'h01)
      $fatal(1, "o=%h %h %h", o.n, o.pa, o.v);

    s = '{default: 4'h4};
    if (s !== 20'h44444) $fatal(1, "s=%h", s);

    ua2 = '{{0, 1}, {2, 3}};
    if (ua2[0][0] !== 0 || ua2[0][1] !== 1 || ua2[1][0] !== 2 || ua2[1][1] !== 3)
      $fatal(1, "ua2 mismatch");

    ua3 = '{{{1, 2}, {3, 4}}, '{'{5, 6}, {7, 8}}};
    if (ua3[0][0][1] !== 2 || ua3[0][1][0] !== 3 || ua3[1][1][1] !== 8)
      $fatal(1, "ua3 mismatch");

    ud = '{{3{1}}, {3{2}}};
    if (ud[0][2] !== 1 || ud[1][0] !== 2) $fatal(1, "ud1 mismatch");

    ud = '{2{{1, 2, 3}}};
    if (ud[0][2] !== 3 || ud[1][1] !== 2) $fatal(1, "ud2 mismatch");

    w = '{{4'h1, 4'h2}, {4'h3, 4'h4}};
    if (w[0] !== 8'h12 || w[1] !== 8'h34) $fatal(1, "w mismatch");

    $display("PASSED");
  end
endmodule
