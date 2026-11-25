package err_pkg;
  class tracker;
    static tracker inst;
    int err_count;
    function new();
      err_count = 0;
    endfunction
    static function tracker get();
      if (inst == null)
        inst = new();
      return inst;
    endfunction
  endclass

  function automatic void report_err(input int tag);
    tracker t = tracker::get();
    t.err_count += (tag >= 0) ? 1 : 0;
  endfunction
endpackage

module svcombassert1;
  logic [7:0] a, b;
  logic       en;
  logic [8:0] sum_o;
  int         action_tag = 1;
  int         comb_execs = 0;

  // Immediate and deferred assertions whose action blocks call into a package
  // class singleton must not force always_comb to poll or trigger on action
  // block variables.
  always_comb begin
    sum_o = en ? ({1'b0, a} + {1'b0, b}) : 9'd0;
    assert (en !== 1'b1 || (sum_o >= {1'b0, a}))
      else err_pkg::report_err(action_tag);
    assert #0 (en !== 1'b1 || (sum_o >= {1'b0, b}))
      else err_pkg::report_err(action_tag);
  end

  always @(sum_o)
    comb_execs++;

  initial begin
    automatic int errors = 0;
    int prev_execs;
    a = 8'd0;
    b = 8'd0;
    en = 1'b1;
    #1;

    // Toggling a variable referenced only inside the assertion action block
    // must not re-evaluate always_comb.
    prev_execs = comb_execs;
    action_tag = 42;
    #1;
    action_tag = 99;
    #1;
    if (comb_execs != prev_execs) begin
      $display("FAIL: action block variable triggered always_comb (%0d -> %0d)",
               prev_execs, comb_execs);
      errors++;
    end

    for (int iter = 0; iter < 15000; iter++) begin
      logic [8:0] exp_sum;
      a = iter[7:0];
      b = (iter * 5 + 1) & 8'hff;
      en = (iter[2:0] != 3'b000);
      #1;
      exp_sum = en ? ({1'b0, a} + {1'b0, b}) : 9'd0;
      if (sum_o !== exp_sum) begin
        $display("FAIL iter=%0d sum_o=%h exp=%h", iter, sum_o, exp_sum);
        errors++;
        if (errors > 5) $fatal(1, "too many errors");
      end
    end

    if (err_pkg::tracker::get().err_count != 0) begin
      $display("FAIL: unexpected assertion failure count %0d",
               err_pkg::tracker::get().err_count);
      errors++;
    end

    if (errors != 0)
      $fatal(1, "FAIL errors=%0d", errors);
    $display("PASSED");
    $finish;
  end
endmodule
