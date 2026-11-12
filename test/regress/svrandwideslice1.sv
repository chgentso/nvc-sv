// An equality constraint on a slice of a variable wider than 64 bits is
// solved whatever the position of the slice (IEEE 1800-2017 18.5), also when
// it lies inside one 64-bit chunk of the variable and does not start on a
// chunk boundary.
class svrandwideslice1_c;
  rand bit [107:0] v;
  bit [15:0] want_lo, want_mid;
  constraint k {
    v[35:20] == want_lo;
    v[100:85] == want_mid;
    v[63:60] == 4'ha;
  }
endclass

module svrandwideslice1;
  initial begin
    automatic svrandwideslice1_c o = new;
    automatic int errs = 0;
    for (int i = 0; i < 20; i++) begin
      o.want_lo = (i % 3 == 0) ? 16'h0001 : (i % 3 == 1) ? 16'h8000 : 16'hcdef;
      o.want_mid = i * 997;
      if (!o.randomize()) begin
        errs++;
        $error("randomize failed with %h %h", o.want_lo, o.want_mid);
      end
      else if (o.v[35:20] !== o.want_lo || o.v[100:85] !== o.want_mid
               || o.v[63:60] !== 4'ha) begin
        errs++;
        $error("wrong value %h", o.v);
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
