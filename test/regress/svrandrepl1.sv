// A replication {N{x}} in a constraint is N copies of x, in the narrow and in
// the wide (> 64 bit) case (IEEE 1800-2017 11.4.12.1). The value used to be
// one copy, so a constraint like v[31:0] == {32{1'b1}} - delay was solved
// with the wrong number.
class svrandrepl1_c;
  rand bit [63:0] cnt;
  rand int unsigned delay;
  rand bit [127:0] wide;
  constraint k {
    delay inside {[1:14]};
    cnt[31:0] == ({32{1'b1}} - delay);
    cnt[63:32] == {32{1'b1}};
    wide[127:64] == {8{8'hA5}};
    wide[63:0] == {4{16'h1234}};
  }
endclass

module svrandrepl1;
  initial begin
    automatic svrandrepl1_c o = new;
    automatic int errs = 0;
    for (int i = 0; i < 10; i++) begin
      if (!o.randomize()) begin errs++; $error("randomize failed"); end
      else begin
        if (o.cnt[31:0] !== 32'hffffffff - o.delay || o.cnt[63:32] !== 32'hffffffff) begin
          errs++;
          $error("cnt %h delay %0d", o.cnt, o.delay);
        end
        if (o.wide !== {{8{8'hA5}}, {4{16'h1234}}}) begin
          errs++;
          $error("wide %h", o.wide);
        end
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
