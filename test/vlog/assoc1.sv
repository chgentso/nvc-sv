module assoc1;
  int x[real];
  int y[event];
  int w[*];
  int r;
  initial begin
    foreach (w[i]) r = 1;
  end
endmodule
