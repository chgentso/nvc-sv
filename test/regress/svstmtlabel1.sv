module svstmtlabel1;
  integer value;

  initial begin
    value = 0;

    L: if (1) begin
      value = 1;
      disable L;
      value = 2;
    end

    M: for (int i = 0; i < 4; i++) begin
      value += 10;
      if (i == 1) disable M;
    end

    if (value !== 21) $fatal(1, "bad value %0d", value);
    $display("PASS");
  end
endmodule
