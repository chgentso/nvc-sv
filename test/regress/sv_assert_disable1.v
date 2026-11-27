module sv_assert_disable1;
  int v;

  initial begin
    v = 0;
    CHK: assert (1) begin
      v = 1;
      disable CHK;
      v = 2;
    end
    if (v !== 1) $fatal(1, "expected 1, got %0d", v);

    CHK2: assert (0) else begin
      v = 10;
      disable CHK2;
      v = 20;
    end
    if (v !== 10) $fatal(1, "expected 10, got %0d", v);

    $display("PASS");
  end
endmodule
