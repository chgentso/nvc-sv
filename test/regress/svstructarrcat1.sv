// Expected values: IEEE 1800-2017 7.2.1 (unpacked structure), 10.9.2 (structure
// assignment patterns), 7.4.2 (unpacked array members), 6.8 (variable initialisers)
typedef struct { int a; int b; int c[4]; } st_t;
module svstructarrcat1;
  st_t s = '{4, 0, {4,5,6,7}};
  st_t t = '{1, 2, '{8,9,10,11}};
  int errs;
  initial begin
    if (s.a !== 4 || s.b !== 0) begin $error("s.a/s.b %0d %0d", s.a, s.b); errs++; end
    if (s.c[0] !== 4 || s.c[1] !== 5 || s.c[2] !== 6 || s.c[3] !== 7) begin $error("s.c wrong"); errs++; end
    if (t.a !== 1 || t.b !== 2) begin $error("t.a/t.b"); errs++; end
    if (t.c[0] !== 8 || t.c[1] !== 9 || t.c[2] !== 10 || t.c[3] !== 11) begin $error("t.c wrong"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
