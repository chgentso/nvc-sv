// The values of a dist for an unsigned variable are converted to unsigned
// before they are extended to its width (IEEE 1800-2017 11.6 and 11.8.2): the
// signed 32 bit value -1 is 32'hffffffff for a 64 bit variable, not all ones.
class svranddistunsigned1_c;
  localparam int W = 64;
  rand bit [63:0] z, r;
  constraint k {
    z dist {0 :/ 1, (2**W-1) :/ 1};
    r dist {[1:(2**W-2)] :/ 1};
  }
endclass

module svranddistunsigned1;
  initial begin
    automatic svranddistunsigned1_c o = new;
    automatic int errs = 0, zlow = 0, zhigh = 0;
    for (int i = 0; i < 40; i++) begin
      if (!o.randomize()) begin errs++; $error("randomize failed"); end
      else begin
        if (o.z === 64'h0) zlow++;
        else if (o.z === 64'h00000000ffffffff) zhigh++;
        else begin errs++; $error("z = %h", o.z); end
        if (o.r < 1 || o.r > 64'hfffffffe) begin errs++; $error("r = %h", o.r); end
      end
    end
    if (zlow == 0 || zhigh == 0) begin errs++; $error("z low %0d high %0d", zlow, zhigh); end
    if (errs == 0) $display("PASSED");
  end
endmodule
