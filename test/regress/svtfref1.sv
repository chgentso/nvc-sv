// Expected values: IEEE 1800-2017 13.5.2 (pass by reference: ref array of handles, ref queue), 13.5 (input/output
// arguments declared in task body), 8.7 (class construction), 12.7.3 (foreach)
class svtfref1_P; int v; function new(int x); v = x; endfunction endclass
module svtfref1;
  task automatic bump;
    ref svtfref1_P src[];
    input [31:0] n;
    output int total;
    begin
      int i;
      total = 0;
      for (i = 0; i < src.size(); i++) begin src[i].v += n; total += src[i].v; end
    end
  endtask
  function automatic int tot(ref int q[$]);
    int s = 0;
    foreach (q[i]) s += q[i];
    return s;
  endfunction
  svtfref1_P ps[];
  int t, qq[$];
  int errs;
  initial begin
    ps = new[3];
    foreach (ps[i]) ps[i] = new(i * 10);
    bump(ps, 5, t);
    if (ps[0].v !== 5 || ps[1].v !== 15 || ps[2].v !== 25 || t !== 45) begin $error("bump %0d %0d %0d t=%0d", ps[0].v, ps[1].v, ps[2].v, t); errs++; end
    qq = {4, 5, 6};
    if (tot(qq) !== 15) begin $error("tot %0d", tot(qq)); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
