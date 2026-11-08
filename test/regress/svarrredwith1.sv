// Self-checking. Expected values: IEEE 1800-2017 7.12.3 (array reduction methods with a with clause: the
// expression is evaluated per element and the results are reduced; the result has the type of the expression),
// 8.? (class handles as elements), 11.4.8 (bitwise and/or/xor on 8-bit values).
// Left out (not defined by the LRM): the reduction of an empty array.
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0h expected %0h", tag, got, exp); end
class fld;
  int unsigned w;
  bit [7:0] m;
  function new(int unsigned w, bit [7:0] m); this.w = w; this.m = m; endfunction
endclass

module svarrredwith1;
  int errs = 0;
  fld q[$];
  fld arr[2];
  function automatic void mk(int unsigned w, bit [7:0] m);
    fld f = new(w, m);
    q.push_back(f);
  endfunction
  initial begin
    int total, prod, e;
    bit [7:0] orv, andv, xorv;
    mk(3, 8'h0f); mk(4, 8'h3c); mk(5, 8'hf3);
    total = q.sum() with (int'(item.w));
    prod = q.product() with (int'(item.w));
    orv = q.or() with (item.m);
    andv = q.and() with (item.m);
    xorv = q.xor() with (item.m);
    `CHK("sum", total, 12)
    `CHK("product", prod, 60)
    `CHK("or", orv, 8'hff)
    `CHK("and", andv, 8'h00)
    `CHK("xor", xorv, 8'hc0)
    arr[0] = new(7, 1); arr[1] = new(9, 2);
    `CHK("fixed array sum", arr.sum() with (int'(item.w) * 2), 32)
    q.delete();
    e = q.sum() with (int'(item.w));   // value not checked; must simply evaluate
    if (errs == 0) $display("PASSED");
  end
endmodule
