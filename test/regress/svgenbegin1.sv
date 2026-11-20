module svgenbegin1;
  logic a, b, c;

  generate
    begin : named_blk
      logic local_x;
      assign local_x = 1'b1;
      assign a = local_x;
    end

    begin
      logic local_x;
      assign local_x = 1'b0;
      assign b = local_x;
    end
  endgenerate

  generate
    begin
      begin : nested_blk
        assign c = 1'b1;
      end
    end
  endgenerate

  initial begin
    #1;
    if (a !== 1'b1) $fatal(1, "a=%b", a);
    if (b !== 1'b0) $fatal(1, "b=%b", b);
    if (c !== 1'b1) $fatal(1, "c=%b", c);
    $display("PASSED");
    $finish;
  end
endmodule
