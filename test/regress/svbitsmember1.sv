// $bits of member selects, part selects and concatenations of packed-struct ports in localparams (self-checking).
// Expected values: IEEE 1800-2017 20.6.2 ($bits), 7.2.1 (packed struct width = sum of members), 11.5.1
// (part select [7:2] is 6 bits), 11.4.12 (concatenation width), 11.4.12.1 (replication width), 6.20.2.
// Reference model: req_t = 8+5 = 13 bits; outer_t = 13+3 = 16 bits.  a_source = 8, a_size = 5,
// {a_source, a_size[3:0]} = 12, tl_i = 13, o_i.r.a_source = 8, o_i[7:2] = 6, {3{a_size}} = 15, w = 8;
// v = 12, arr[0] (int) = 32, r.a_size = 5.
package tl_pkg;
  typedef struct packed { logic [7:0] a_source; logic [4:0] a_size; } req_t;
  typedef struct packed { req_t r; logic [2:0] k; } outer_t;
endpackage
module sub(input tl_pkg::req_t tl_i, input tl_pkg::outer_t o_i);
  localparam int IW  = $bits(tl_i.a_source);
  localparam int SZW = $bits(tl_i.a_size);
  localparam int CAT = $bits({tl_i.a_source, tl_i.a_size[3:0]});
  localparam int ALL = $bits(tl_i);
  localparam int NEST = $bits(o_i.r.a_source);
  localparam int PART = $bits(o_i[7:2]);
  localparam int REP = $bits({3{tl_i.a_size}});
  logic [IW-1:0] w;
  int bad = 0;
  initial begin
    if (IW != 8) begin bad++; $error("IW = %0d", IW); end
    if (SZW != 5) begin bad++; $error("SZW = %0d", SZW); end
    if (CAT != 12) begin bad++; $error("CAT = %0d", CAT); end
    if (ALL != 13) begin bad++; $error("ALL = %0d", ALL); end
    if (NEST != 8) begin bad++; $error("NEST = %0d", NEST); end
    if (PART != 6) begin bad++; $error("PART = %0d", PART); end
    if (REP != 15) begin bad++; $error("REP = %0d", REP); end
    if ($bits(w) != 8) begin bad++; $error("$bits(w) = %0d", $bits(w)); end
  end
endmodule
module svbitsmember1;
  tl_pkg::req_t r;
  tl_pkg::outer_t o;
  logic [11:0] v;
  int arr[4];
  localparam int LV = $bits(v);
  localparam int LA = $bits(arr[0]);
  int errs = 0;
  sub u(.tl_i(r), .o_i(o));
  initial begin
    #1;
    if (LV != 12) begin errs++; $error("LV = %0d", LV); end
    if (LA != 32) begin errs++; $error("LA = %0d", LA); end
    if ($bits(r.a_size) != 5) begin errs++; $error("$bits(r.a_size) = %0d", $bits(r.a_size)); end
    if (u.bad != 0) errs++;
    if (errs == 0) $display("PASSED");
  end
endmodule
