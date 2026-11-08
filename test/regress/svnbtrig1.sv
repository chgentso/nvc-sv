// IEEE 1800-2017 15.5.2: ->> triggers an event as a nonblocking assignment
// would, in the NBA region of the current time step (or later with an
// intra-assignment delay or event control), so a process that starts
// waiting later in the same active region still sees the trigger, and the
// trigger does not block the triggering process.
module svnbtrig1;
  event e1, e2, e3;
  logic clk = 0;
  int log_[$];
  time t2, t3;
  int n3 = 0;

  // Waits on e1 only after the triggering process has run in the active
  // region: a blocking -> would be missed, ->> must be seen.
  initial begin
    #10;
    #0;                        // Inactive region: after the ->> below
    @(e1) log_.push_back(1);
  end

  initial begin
    #10;
    ->> e1;
    log_.push_back(0);         // Not blocked by the trigger
  end

  // Intra-assignment delay: trigger 5 time units later
  initial begin
    #20;
    ->> #5 e2;
    if ($time != 20) $fatal(1, "->> #5 blocked the process");
  end
  always @(e2) t2 = $time;

  // Intra-assignment event control: trigger at the next posedge clk
  initial begin
    #31;
    ->> @(posedge clk) e3;
    ->> repeat (2) @(posedge clk) e3;
  end
  always #4 clk = ~clk;        // posedge at 4, 12, 20, 28, 36, 44, ...
  always @(e3) begin n3++; t3 = $time; end

  // e1.triggered is set in the NBA region, not when ->> executes
  bit trig_now, trig_nba;
  initial begin
    #50;
    ->> e1;
    trig_now = e1.triggered;
    #0 trig_now |= e1.triggered;
  end
  always @(e1) if ($time == 50) trig_nba = e1.triggered;

  initial begin
    #60;
    if (log_.size() != 2 || log_[0] != 0 || log_[1] != 1)
      $fatal(1, "nonblocking trigger order %p", log_);
    if (t2 != 25) $fatal(1, "->> #5 triggered at %0d", t2);
    if (n3 != 2 || t3 != 44)
      $fatal(1, "->> @(posedge clk) triggered %0d times, last at %0d", n3,
             t3);
    if (trig_now) $fatal(1, "triggered set before the NBA region");
    if (!trig_nba) $fatal(1, "triggered not set in the NBA region");
    $display("PASSED");
    $finish;
  end
endmodule
