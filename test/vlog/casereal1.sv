module casereal1;
  typedef struct { int a; real r; } s_t;
  typedef struct { s_t s; bit b; } n_t;
  typedef struct { int a; logic [3:0] b; } i_t;
  s_t a, b;
  n_t c, d;
  i_t e, f;
  real x, y;
  initial begin
    if (a === b) ;                      // Error
    if (c !== d) ;                      // Error
    if (e === f) ;                      // OK
    if (a == b || a != b) ;             // OK
    if (x === y) ;                      // Error
  end
endmodule
