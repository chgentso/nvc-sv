// A fixed size unpacked array of reals converted to a dynamic array or
// queue by assignment, by new[] with an initialiser, and as an argument
// IEEE 1800-2017 7.5.1 (new[] with an initialising array copies as many
// elements as fit), 7.6 (assignment of unpacked arrays of equivalent
// element type), 13.5 (argument passing by value)
module svrealdyn1;
  real rr[3] = '{1.0, 2.0, 3.5};
  real rd[], rq[$];
  int errs;
  function automatic real sum(real a[]);
    real s = 0.0;
    foreach (a[i]) s += a[i];
    return s;
  endfunction
  initial begin
    rd = new[3](rr);
    if (rd.size() != 3 || rd[0] != 1.0 || rd[2] != 3.5) begin errs++; $display("FAILED new[3] %p", rd); end
    rd = new[4](rr);
    if (rd.size() != 4 || rd[2] != 3.5 || rd[3] != 0.0) begin errs++; $display("FAILED new[4] %p", rd); end
    rd = new[2](rr);
    if (rd.size() != 2 || rd[1] != 2.0) begin errs++; $display("FAILED new[2] %p", rd); end
    rd = rr;
    if (rd.size() != 3 || rd[1] != 2.0) begin errs++; $display("FAILED assign %p", rd); end
    rq = rr;
    if (rq.size() != 3 || rq[2] != 3.5) begin errs++; $display("FAILED queue %p", rq); end
    if (sum(rr) != 6.5) begin errs++; $display("FAILED argument %f", sum(rr)); end
    if (errs == 0) $display("PASSED");
  end
endmodule
