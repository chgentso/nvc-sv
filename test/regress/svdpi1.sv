// DPI-C imports: scalar, real, bit/logic vector, string and chandle arguments, alias, task.
// Expected values: IEEE 1800-2017 35.5 and Annex H (argument passing and the canonical
// svBitVecVal / svLogicVecVal layout: 0=(a0,b0) 1=(a1,b0) z=(a0,b1) x=(a1,b1)), 6.11.
// The C side is test/dpi/dpi_test.c; each expected value is what that C body computes
// for the mapped arguments.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpi1;
  import "DPI-C" function int dpi_add(input int a, input int b);
  import "DPI-C" function longint dpi_ladd(input longint a, input longint b);
  import "DPI-C" function int unsigned dpi_uadd(input int unsigned a, input int unsigned b);
  import "DPI-C" function byte dpi_neg8(input byte a);
  import "DPI-C" function shortint dpi_neg16(input shortint a);
  import "DPI-C" function byte unsigned dpi_inc8(input byte unsigned a);
  import "DPI-C" pure function real dpi_scale(input real a, input real b);
  import "DPI-C" function shortreal dpi_halve(input shortreal a);
  import "DPI-C" function bit dpi_not(input bit a);
  import "DPI-C" function logic dpi_lnot(input logic a);
  import "DPI-C" function void dpi_divmod(input int a, input int b, output int q, output int r);
  import "DPI-C" function void dpi_inc(inout int x);
  import "DPI-C" function int dpi_popcount(input bit [39:0] v);
  import "DPI-C" function void dpi_bv_rev(input bit [39:0] i, output bit [39:0] o);
  import "DPI-C" function void dpi_lv_swap(input logic [7:0] i, output logic [7:0] o);
  import "DPI-C" function int dpi_lv_known(input logic [7:0] i);
  import "DPI-C" function void dpi_lv_inout(inout logic [7:0] v);
  import "DPI-C" function void dpi_wide(input bit [63:0] i, output bit [63:0] o);
  import "DPI-C" function string dpi_greet(input string name);
  import "DPI-C" function int dpi_strlen(input string s);
  import "DPI-C" function void dpi_upper(input string i, output string o);
  import "DPI-C" function chandle dpi_new(input int n);
  import "DPI-C" function int dpi_get(input chandle h);
  import "DPI-C" function void dpi_set(input chandle h, input int n);
  import "DPI-C" function void dpi_free(input chandle h);
  import "DPI-C" context function int dpi_counter();
  import "DPI-C" dpi_add = function int add_alias(input int x, input int y);
  import "DPI-C" task dpi_task_sum(input int a, input int b, output int sum);

  typedef enum logic [1:0] { A, B, C } e_t;

  int q, r, x, s, c1, c2;
  bit [39:0] bv;
  logic [7:0] lv, lo;
  bit [63:0] w;
  string str;
  chandle h;
  real rv;
  shortreal sr;
  int err = 0;

  initial begin
    `CHK(dpi_add(3, 4) == 7, "add")
    `CHK(dpi_add(-3, -4) == -7, "add neg")
    `CHK(dpi_ladd(64'h1_0000_0000, 5) == 64'h1_0000_0005, "ladd")
    `CHK(dpi_uadd(32'hffff_fff0, 32'h20) == 32'd16, "uadd wraps modulo 2**32")
    `CHK(dpi_neg8(8'sd100) == -100, "neg8")
    `CHK(dpi_neg16(-16'sd300) == 300, "neg16")
    `CHK(dpi_inc8(8'd255) == 0, "inc8 wraps")
    rv = dpi_scale(1.5, 4.0);
    `CHK(rv == 6.0, "scale")
    sr = dpi_halve(5.0);
    `CHK(sr == 2.5, "halve")
    `CHK(dpi_not(1'b0) === 1'b1 && dpi_not(1'b1) === 1'b0, "not")
    `CHK(dpi_lnot(1'b0) === 1'b1, "lnot 0")
    `CHK(dpi_lnot(1'b1) === 1'b0, "lnot 1")
    `CHK(dpi_lnot(1'bx) === 1'bx, "lnot x")
    `CHK(dpi_lnot(1'bz) === 1'bx, "lnot z")
    dpi_divmod(17, 5, q, r);
    `CHK(q == 3 && r == 2, "divmod")
    x = 41;
    dpi_inc(x);
    `CHK(x == 42, "inc")
    `CHK(dpi_popcount(40'hff_0000_ff0f) == 20, "popcount")
    dpi_bv_rev(40'h12_3456_789a, bv);
    `CHK(bv === 40'hed_cba9_8765, "bv_rev (bitwise inverse of the 40 bit value)")
    // the C body swaps the a and b words: result bit = (a_in <-> b_in)
    dpi_lv_swap(8'b01xz_10zx, lo);
    `CHK(lo === 8'b0zx1_z01x, "lv_swap")
    `CHK(dpi_lv_known(8'hff) == 1, "known all")
    `CHK(dpi_lv_known(8'b0000_000x) == 0, "known with x")
    lv = 8'b1010_xz01;
    dpi_lv_inout(lv);
    `CHK(lv === 8'b0101_zx10, "lv_inout (a word inverted: x and z swap)")
    dpi_wide(64'h1111_2222_3333_4444, w);
    `CHK(w === 64'h3333_4444_1111_2222, "wide swaps the 32 bit words")
    str = dpi_greet("world");
    `CHK(str == "hello world", "greet")
    `CHK(dpi_strlen("abcdef") == 6, "strlen")
    dpi_upper("mixed Case", str);
    `CHK(str == "MIXED CASE", "upper")
    h = dpi_new(42);
    `CHK(h != null, "chandle not null")
    `CHK(dpi_get(h) == 42, "get 42")
    dpi_set(h, 7);
    `CHK(dpi_get(h) == 7, "get 7")
    dpi_free(h);
    // the C function increments a static counter on every call; argument
    // evaluation order is not fixed, so the two calls are separate statements
    c1 = dpi_counter();
    c2 = dpi_counter();
    `CHK(c2 == c1 + 1, "counter increments")
    `CHK(add_alias(20, 22) == 42, "alias")
    dpi_task_sum(30, 12, s);
    `CHK(s == 42, "task")
    `CHK(dpi_add(B, C) == 3, "enum")
    if (err == 0) $display("PASSED");
  end
endmodule
