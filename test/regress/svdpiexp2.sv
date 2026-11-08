// Exported DPI tasks that consume time, called from imported context tasks.
// IEEE 1800-2017 35.5.2 / 35.6 / 35.7 (an imported task may call exported
// tasks; an exported task may consume simulation time, and the calling
// imported task then also consumes time), 35.5.3 (context imports may call
// exports), 35.9 (an exported task returns 0 when it is not disabled).
// Several processes may be suspended in foreign code at once.
// C side: test/dpi/dpi_export2.c.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpiexp2;
  timeunit 1ns;
  timeprecision 1ns;

  export "DPI-C" task sv_delay;
  export "DPI-C" task sv_wait_ev;
  export "DPI-C" task sv_nodelay;

  import "DPI-C" context task dpi_tdelay(input int step, input int count,
                                         output int last, output int bad);
  import "DPI-C" context task dpi_twait(output int value);
  import "DPI-C" context task dpi_tmix(input int a, output int r);

  event ev;
  int shared_value;
  int calls = 0;

  task sv_delay(input int n, output int now);
    calls++;
    #n;
    now = int'($time);
  endtask

  task sv_wait_ev(output int value);
    @ev;
    value = shared_value;
  endtask

  task sv_nodelay(input int a, output int b);
    b = 2 * a;
  endtask

  int err = 0;
  int last, bad, l1, b1, l2, b2, v, r;
  time t0;

  initial begin
    dpi_tdelay(10, 3, last, bad);
    `CHK($time == 30, "time after three 10ns exported delays")
    `CHK(last == 30 && bad == 0, "times seen by the C code")
    `CHK(calls == 3, "export called three times")

    // Two processes suspended in foreign code at the same time
    t0 = $time;
    fork
      dpi_tdelay(5, 4, l1, b1);
      dpi_tdelay(7, 2, l2, b2);
    join
    `CHK($time == t0 + 20, "join after the longer call")
    `CHK(l1 == t0 + 20 && b1 == 0, "first process")
    `CHK(l2 == t0 + 14 && b2 == 0, "second process")

    // Blocking on an event inside the exported task
    t0 = $time;
    fork
      dpi_twait(v);
      begin
        #3 shared_value = 42;
        ->ev;
      end
    join
    `CHK(v == 42 && $time == t0 + 3, "value delivered by the event")

    // A zero-time export followed by a time-consuming one
    t0 = $time;
    dpi_tmix(6, r);
    `CHK(r == 12 * 1000 + t0 + 1, "mixed exports")
    `CHK($time == t0 + 1, "time after mixed exports")

    if (err == 0) $display("PASSED");
  end
endmodule
