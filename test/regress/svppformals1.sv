// Macros with formal arguments split over several lines, and with default arguments
// Expected values: IEEE 1800-2017 22.5.1 (`define with formal arguments, default values, empty actuals)
`define mac3(a_, b_,
             c_) got = $sformatf("%0d %0d %0d", a_, b_, c_)
`define mac2(
   x_ = 7,
   y_ = 8
) got = $sformatf("%0d %0d", x_, y_)
module svppformals1;
  string got;
  int errs = 0;
  task automatic chk(string exp);
    if (got != exp) begin $error("got '%s' expected '%s'", got, exp); errs++; end
  endtask
  initial begin
    `mac3(1, 2, 3);
    chk("1 2 3");
    `mac2(1, 2);
    chk("1 2");
    `mac2(, 5);
    chk("7 5");
    `mac2();
    chk("7 8");
    if (errs == 0) $display("PASSED");
  end
endmodule
