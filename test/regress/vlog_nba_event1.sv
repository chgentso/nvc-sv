module vlog_nba_event1;
  reg clk = 0;
  reg [7:0] a = 0;
  reg [7:0] b = 0;
  real r = 0.0;
  event ev;

  always #5 clk = ~clk;

  task automatic sched(input int n, input [7:0] val);
    a <= repeat(n) @(posedge clk) val;
  endtask

  initial begin
    sched(2, 8'h5a);
    b <= @(ev) 8'ha5;
    r <= repeat(64'h8000000000000001) @(ev) 3.5;

    @(posedge clk);
    #1;
    if (a !== 8'h00) $fatal(1, "after 1 clk a=%h", a);
    if (b !== 8'h00) $fatal(1, "before ev b=%h", b);

    ->ev;
    #1;
    if (b !== 8'ha5) $fatal(1, "after ev b=%h", b);
    if (r != 0.0) $fatal(1, "large repeat triggered early r=%f", r);

    @(posedge clk);
    #1;
    if (a !== 8'h5a) $fatal(1, "after 2 clk a=%h", a);

    // Repeat count 0 schedules immediately without waiting
    a <= repeat(0) @(posedge clk) 8'h33;
    #1;
    if (a !== 8'h33) $fatal(1, "repeat 0 a=%h", a);

    $display("PASSED");
    $finish;
  end
endmodule
