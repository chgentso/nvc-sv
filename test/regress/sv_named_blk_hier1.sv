module sv_named_blk_sub;
  initial begin : first
    static int n = 1;
    n = n + 2;
  end
  initial begin : second
    static int n = 10;
    n = n + 5;
  end
endmodule

module sv_named_blk_hier1;
  sv_named_blk_sub sub();
  string scope;
  reg [7:0] arr [0:1];

  initial begin
    A: assert (1) begin : B
      reg [7:0] val;
      val = 8'd11;
    end
    C: assume (0) else begin : B
      reg [7:0] val;
      val = 8'd22;
    end

    FOR: for (int i = 0; i < 2; i = i + 1)
      FOREACH: foreach (arr[j]) begin : INNER
        static reg [7:0] val;
        val = 2 * i + j;
        $sformat(scope, "%m");
      end

    if (1) begin : local_blk
      static int x = 7;
      x = x + 1;
    end

    #1;
    if (sub.first.n !== 3 || sub.second.n !== 15)
      $fatal(1, "sub=%0d %0d", sub.first.n, sub.second.n);
    if (local_blk.x !== 8)
      $fatal(1, "local_blk=%0d", local_blk.x);
    if (sv_named_blk_hier1.A.B.val !== 11 || sv_named_blk_hier1.C.B.val !== 22)
      $fatal(1, "assert=%0d %0d", sv_named_blk_hier1.A.B.val, sv_named_blk_hier1.C.B.val);
    if (A.B.val !== 11 || C.B.val !== 22)
      $fatal(1, "local assert=%0d %0d", A.B.val, C.B.val);
    if (scope != "sv_named_blk_hier1.FOR.FOREACH.INNER")
      $fatal(1, "scope=%s", scope);
    if (sv_named_blk_hier1.FOR.FOREACH.INNER.val !== 3)
      $fatal(1, "loop=%0d", sv_named_blk_hier1.FOR.FOREACH.INNER.val);
    $display("PASSED");
  end
endmodule
