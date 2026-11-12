module top;
  let recurse(a) = (a == 1) ? 1 : recurse(a - 1);

  initial begin
    if (recurse(1) != 1) $stop;
  end
endmodule
