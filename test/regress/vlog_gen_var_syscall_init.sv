// A generate block refers to variables of the enclosing module whose
// initial values call system functions
module chk(input logic clk);
  int x = $urandom_range(3, 3);
  string s = $sformatf("%0d", 7);
  int hits;
  for (genvar i = 0; i < 2; i++) begin : g
    always @(posedge clk) begin
      if (x == 3 && s == "7") hits++;
      else $display("FAILED x=%0d s=%s", x, s);
    end
  end
  if (1) begin : h
    initial begin
      #1;
      if (x != 3 || s != "7") $display("FAILED init x=%0d s=%s", x, s);
    end
  end
endmodule

module vlog_gen_var_syscall_init;
  logic clk = 0;
  chk c(clk);
  initial begin
    #2 clk = 1;
    #2;
    if (c.hits == 2) $display("PASSED");
    else $display("FAILED hits=%0d", c.hits);
  end
endmodule
