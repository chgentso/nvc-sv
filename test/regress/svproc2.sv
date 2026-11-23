// IEEE 1800-2017 18.14.2: a forked process takes its stream from its parent when
// the parent has one, and does not share later draws of the parent.
module svproc2;
  int errs = 0;
  int ra, rb, rc;

  task automatic check(bit ok, string what);
    if (!ok) begin $error("%s", what); errs++; end
  endtask

  task automatic run(output int first, output int second);
    process p;
    p = process::self();
    p.srandom(77);
    fork
      begin
        first = $urandom;
        #1;
        second = $urandom;
      end
    join_none
    // The parent draws between the first and the second draw of the child
    void'($urandom); void'($urandom);
    #2;
  endtask

  initial begin
    int a1, a2, b1, b2;
    run(a1, a2);
    run(b1, b2);
    check(a1 == b1, "child first draw");
    check(a2 == b2, "child draws are independent of the parent");
    check(a1 != a2, "child draws differ");

    // Two branches seeded the same way agree
    fork
      begin process p; p = process::self(); p.srandom(42); ra = $urandom; end
      begin process p; p = process::self(); p.srandom(42); rb = $urandom; end
      begin process p; p = process::self(); p.srandom(43); rc = $urandom; end
    join
    check(ra == rb, "same seed in two branches");
    check(ra != rc, "different seed in a branch");
    if (errs == 0) $display("PASSED");
  end
endmodule
