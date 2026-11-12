// Feedback through real arithmetic becomes stable

module svrealfeedback1;

  real sum, difference, product, quotient, power, negative;

  assign sum = sum + 0.0;
  assign difference = difference - 0.0;
  assign product = product * 1.0;
  assign quotient = quotient / 1.0;
  assign power = power ** 1.0;
  assign negative = -negative;

  initial begin
    #1;
    if (sum == 0.0 && difference == 0.0 && product == 0.0 &&
        quotient == 0.0 && power == 0.0 && negative == 0.0) begin
      $display("PASS");
    end else begin
      $display("FAILED: expected zero-valued feedback loops");
    end
  end

endmodule
