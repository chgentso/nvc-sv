module final1;
  logic clk;
  int n;
  final begin
    #1 n = 1;
    @(posedge clk) n = 2;
    wait (n == 3);
  end
endmodule
