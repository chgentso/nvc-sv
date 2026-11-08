// defparam on instance-array elements and generate-block instances.
// Expected values: IEEE 1800-2017 23.10.1 (defparam), 23.3.2 (instance arrays), 27 (generate blocks),
// 23.6 (hierarchical references to parameters). Each leaf records the time its parameter-dependent delay
// expired. Hierarchical-name text (%m) and same-time ordering are not checked.
module leaf #(parameter A = 1, parameter B = 2) ();
  time fired = 0;
  initial begin #(A*100 + B); fired = $time; end
endmodule
module mid #(parameter N = 2) ();
  leaf l();
  for (genvar i = 0; i < N; i++) begin : g
    leaf u();
  end
endmodule
module svdefparam2;
  leaf arr[3]();
  mid m();
  defparam arr[1].A = 5;
  defparam m.g[1].u.B = 9;
  defparam m.g[0].u.A = 7, m.l.B = 4;
  int errs = 0;
  task automatic chk(string what, int got, int exp);
    if (got !== exp) begin errs++; $error("%s: got %0d, expected %0d", what, got, exp); end
  endtask
  task automatic leafchk(string n, int a, int b, int ea, int eb, time f);
    chk({n, ".A"}, a, ea); chk({n, ".B"}, b, eb); chk({n, " fired"}, int'(f), ea*100 + eb);
  endtask
  initial begin
    #1000;
    leafchk("arr[0]", arr[0].A, arr[0].B, 1, 2, arr[0].fired);
    leafchk("arr[1]", arr[1].A, arr[1].B, 5, 2, arr[1].fired);
    leafchk("arr[2]", arr[2].A, arr[2].B, 1, 2, arr[2].fired);
    leafchk("m.l", m.l.A, m.l.B, 1, 4, m.l.fired);
    leafchk("m.g[0].u", m.g[0].u.A, m.g[0].u.B, 7, 2, m.g[0].u.fired);
    leafchk("m.g[1].u", m.g[1].u.A, m.g[1].u.B, 1, 9, m.g[1].u.fired);
    if (errs == 0) $display("PASSED");
  end
endmodule
