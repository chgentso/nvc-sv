// Type keys in assignment patterns with nested types and parameters
// Expected values: IEEE 1800-2017 10.9.1 (type keys and default in assignment patterns; index keys override
// default), 6.19 (enum value of B is 2), 6.20.2 (localparam initialised by a pattern), 7.4.5
// Line I (a type key reaching the elements of nested unpacked arrays) assumes the key applies to every element of
// the innermost type.

typedef enum {R, G, B} col_t;
typedef struct { int a; int b; } ms_t;
typedef struct { ms_t m; col_t c; int k; real r; } o_t;
module svtypekey2;
  localparam ms_t LP = '{int:4, default:0};
  localparam ms_t LP2 = '{b:9, int:1};
  o_t o;
  ms_t sa [3];
  bit [3:0] pbits;
  logic [3:0] lbits;
  byte b3 [2][2];
  int errs;
  initial begin
    if (LP.a != 4 || LP.b != 4 || LP2.a != 1 || LP2.b != 9) begin $error("A %0d %0d %0d %0d", LP.a, LP.b, LP2.a, LP2.b); errs++; end
    o = '{ms_t:'{a:1, b:2}, col_t:B, int:7, real:1.5};
    if (o.m.a != 1 || o.m.b != 2 || o.c != B || o.k != 7 || o.r != 1.5) begin $error("B %0d %0d %0d %0d %f", o.m.a, o.m.b, o.c, o.k, o.r); errs++; end
    sa = '{ms_t:'{int:3}};
    if (sa[0].a != 3 || sa[1].b != 3 || sa[2].a != 3) begin $error("C"); errs++; end
    sa = '{1:'{int:8}, default:'{a:5, b:6}};
    if (sa[0].a != 5 || sa[0].b != 6 || sa[1].a != 8 || sa[2].b != 6) begin $error("D"); errs++; end
    pbits = '{bit:1};
    if (pbits !== 4'b1111) begin $error("E %b", pbits); errs++; end
    lbits = '{logic:1};
    if (lbits !== 4'b1111) begin $error("F %b", lbits); errs++; end
    lbits = '{bit:1, default:0};
    if (lbits !== 4'b0000) begin $error("G %b", lbits); errs++; end
    b3 = '{byte:5};
    if (b3[0][0] != 5 || b3[1][1] != 5) begin $error("I %0d %0d", b3[0][0], b3[1][1]); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
