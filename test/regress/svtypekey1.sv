// Type keys in assignment patterns of structures and arrays
// Expected values: IEEE 1800-2017 10.9.1 (structure and array assignment patterns: member name and index keys
// take priority over type keys, type keys over default; a typedef is the same type as its base), 6.18, 7.2.1.
// Patterns that repeat a key of the same type (B, E) leave the winner open: both members must agree.

typedef int myint;
typedef logic [3:0] nib_t;
typedef bit [3:0] bnib_t;
typedef struct { int a; int b; } ms_t;
typedef struct { int i; byte y; string s; real r; nib_t l; myint m; shortint si; } mix_t;
typedef struct packed { nib_t x; bnib_t y; } pk_t;
module svtypekey1;
  ms_t ms;
  mix_t mx;
  pk_t pk;
  int arr [4];
  byte barr [3];
  string sarr [2];
  int errs;
  initial begin
    ms = '{ default:1, int:5};
    if (ms.a != 5 || ms.b != 5) begin $error("A %0d %0d", ms.a, ms.b); errs++; end
    ms = '{ int:2, int:3};
    if (ms.a != ms.b || (ms.a != 2 && ms.a != 3)) begin $error("B %0d %0d", ms.a, ms.b); errs++; end
    ms = '{ a:7, int:3};
    if (ms.a != 7 || ms.b != 3) begin $error("C %0d %0d", ms.a, ms.b); errs++; end
    mx = '{ int:1, byte:2, string:"x", real:2.5, nib_t:4'h9, shortint:4, default:0 };
    if (mx.i != 1 || mx.y != 2 || mx.s != "x" || mx.r != 2.5 || mx.l !== 4'h9 || mx.m != 1 || mx.si != 4) begin
      $error("D %0d %0d %s %f %h %0d %0d", mx.i, mx.y, mx.s, mx.r, mx.l, mx.m, mx.si); errs++;
    end
    mx = '{ myint:8, int:1, default:3 };
    if (mx.i != mx.m || (mx.i != 1 && mx.i != 8) || mx.y != 3 || mx.si != 3) begin $error("E %0d %0d %0d %0d", mx.i, mx.m, mx.y, mx.si); errs++; end
    mx = '{ default:6, i:9 };
    if (mx.i != 9 || mx.m != 6 || mx.y != 6 || mx.si != 6) begin $error("F %0d %0d %0d %0d", mx.i, mx.m, mx.y, mx.si); errs++; end
    pk = '{ nib_t:4'h5, bnib_t:4'ha };
    if (pk !== 8'h5a) begin $error("G %h", pk); errs++; end
    arr = '{ int:3, default:0 };
    if (arr[0] != 3 || arr[1] != 3 || arr[2] != 3 || arr[3] != 3) begin $error("H"); errs++; end
    arr = '{ 1:5, int:2 };
    if (arr[0] != 2 || arr[1] != 5 || arr[2] != 2 || arr[3] != 2) begin $error("I"); errs++; end
    arr = '{ 1:5, default:2 };
    if (arr[0] != 2 || arr[1] != 5 || arr[2] != 2 || arr[3] != 2) begin $error("J"); errs++; end
    barr = '{ int:1, byte:2 };
    if (barr[0] != 2 || barr[1] != 2 || barr[2] != 2) begin $error("K %0d %0d %0d", barr[0], barr[1], barr[2]); errs++; end
    sarr = '{ string:"p", default:"q" };
    if (sarr[0] != "p" || sarr[1] != "p") begin $error("L %s %s", sarr[0], sarr[1]); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
