module svcombstatic1;
  logic [7:0] a, b;
  logic [15:0] out1, out2, out3;

  function automatic logic [15:0] scale_fn(input logic [7:0] x);
    logic [15:0] tmp;
    tmp = {8'h00, x} + 16'h0011;
    return tmp;
  endfunction

  function logic [15:0] static_fn(input logic [7:0] x);
    logic [15:0] tmp;
    tmp = {x, x} ^ 16'h0f0f;
    return tmp;
  endfunction

  always_comb begin
    logic [7:0] local_sum;
    logic [15:0] nested_res;
    local_sum = a + b;
    if (a[0]) begin
      logic [7:0] inner;
      inner = local_sum ^ 8'h5a;
      nested_res = {inner, local_sum};
    end else begin
      nested_res = {8'h00, local_sum};
    end
    out1 = nested_res;
  end

  always_comb begin
    logic [15:0] acc;
    acc = 16'h0;
    for (int i = 0; i < 4; i++) begin
      logic [15:0] bit_mask;
      bit_mask = (a[i] ^ b[i]) ? (16'h1 << (i * 4)) : 16'h0;
      acc |= bit_mask;
    end
    out2 = acc ^ scale_fn(a);
  end

  always_comb begin
    logic [15:0] tmp_s;
    tmp_s = static_fn(b);
    out3 = tmp_s;
  end

  initial begin
    int errors = 0;
    a = 8'h00;
    b = 8'h00;
    #1;

    for (int iter = 0; iter < 50000; iter++) begin
      logic [7:0] exp_sum;
      logic [15:0] exp_out1, exp_out2, exp_out3;
      a = iter[7:0];
      b = (iter * 3) & 8'hff;
      #1;

      exp_sum = a + b;
      if (a[0])
        exp_out1 = {exp_sum ^ 8'h5a, exp_sum};
      else
        exp_out1 = {8'h00, exp_sum};

      exp_out2 = 16'h0;
      for (int i = 0; i < 4; i++) begin
        if (a[i] ^ b[i])
          exp_out2 |= (16'h1 << (i * 4));
      end
      exp_out2 ^= ({8'h00, a} + 16'h0011);
      exp_out3 = {b, b} ^ 16'h0f0f;

      if (out1 !== exp_out1 || out2 !== exp_out2 || out3 !== exp_out3) begin
        $display("FAIL iter=%0d out1=%h/%h out2=%h/%h out3=%h/%h",
                 iter, out1, exp_out1, out2, exp_out2, out3, exp_out3);
        errors++;
        if (errors > 5) $fatal(1, "too many errors");
      end
    end

    if (errors != 0)
      $fatal(1, "FAIL errors=%0d", errors);
    $display("PASSED");
    $finish;
  end
endmodule
