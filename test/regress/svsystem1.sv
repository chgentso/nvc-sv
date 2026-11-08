// $system runs a shell command and returns its exit status

module svsystem1;
  int r;
  initial begin
    r = $system("exit 0");
    if (r != 0) $fatal(1, "FAILED exit 0: %0d", r);
    r = $system("exit 3");
    if (r == 0) $fatal(1, "FAILED exit 3: %0d", r);
    void'($system("true"));
    $display("PASSED");
  end
endmodule
