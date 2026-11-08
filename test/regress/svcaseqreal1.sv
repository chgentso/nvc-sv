// IEEE 1800-2017 Table 11-1: === and !== do not apply to real operands,
// and aggregate equality is defined member by member (11.4.5), so case
// equality of a structure with a real member is an error.  With --relaxed
// it is a warning and the real member is compared with ==.
module svcaseqreal1;
  typedef struct { int a; real r; } s_t;
  s_t s1 = '{1, 2.5}, s2 = '{1, 2.5}, s3 = '{1, 3.5};
  bit x, y;
  initial begin
    x = s1 === s2;
    y = s1 !== s3;
    if (x === 1 && y === 1) $display("PASSED");
    else $display("FAILED %b %b", x, y);
  end
endmodule
