// A constraint may look up a member of an element of a parameter array of
// packed structures with a random index (IEEE 1800-2017 18.5): the value of
// the member is that of the element which the index selects.
module svrandparamtab1;
  typedef struct packed { logic [7:0] lo; logic [7:0] hi; bit secret; } pi_t;
  localparam pi_t Info [4] = '{ '{8'd0, 8'd3, 1'b0}, '{8'd4, 8'd7, 1'b1},
                                '{8'd8, 8'd11, 1'b0}, '{8'd12, 8'd15, 1'b1} };
  class c;
    rand int unsigned idx;
    rand int unsigned addr;
    constraint k {
      idx < 4;
      addr inside {[Info[idx].lo : Info[idx].hi]};
      if (Info[idx].secret) addr % 2 == 0;
    }
  endclass

  initial begin
    automatic c o = new;
    automatic int errs = 0;
    automatic bit [3:0] seen = 0;
    for (int i = 0; i < 100; i++) begin
      if (!o.randomize()) $fatal(1, "randomize failed");
      if (o.addr < Info[o.idx].lo || o.addr > Info[o.idx].hi) errs++;
      if (Info[o.idx].secret && o.addr % 2 != 0) errs++;
      seen[o.idx] = 1;
    end
    if (errs != 0) $fatal(1, "%0d violations", errs);
    if (seen != 4'hf) $fatal(1, "not every row was chosen: %b", seen);
    $display("PASSED");
  end
endmodule
