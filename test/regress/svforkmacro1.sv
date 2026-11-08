// Expected values: IEEE 1800-2017 22.5.1 (macros with default arguments), 9.3.2 (join_any),
// 9.6.1 (disable fork), 9.4.3 (wait). `done` is set at 50 ns, so every wait finishes at 50 ns and
// none of the timeout/exit message branches may run.
`timescale 1ns/1ps
int bad_msgs = 0;
int tw[1:4];
`define dv_fatal(MSG_, ID_) bad_msgs++;
`define dv_info(MSG_, V_, ID_) bad_msgs++;
`define dv_error(MSG_, ID_) bad_msgs++;
`define gfn "id"
`define DV_SPINWAIT_EXIT(WAIT_, EXIT_, MSG_ = "exit condition occurred!", ID_ =`gfn) \
  begin \
    fork begin \
      fork \
        begin \
          WAIT_ \
        end \
        begin \
          EXIT_ \
          if (MSG_ != "") begin \
            `dv_info(MSG_, 1, ID_) \
          end \
        end \
      join_any \
      disable fork; \
    end join \
  end
`define DV_WAIT_TIMEOUT(TIMEOUT_NS_, ID_  = `gfn, ERROR_MSG_ = "timeout occurred!", REPORT_FATAL_ = 1) \
  begin \
    #(TIMEOUT_NS_ * 1ns); \
    if (REPORT_FATAL_) begin \
      `dv_fatal(ERROR_MSG_, ID_) \
    end else begin \
      `dv_error(ERROR_MSG_, ID_) \
    end \
  end
`define DV_SPINWAIT(WAIT_, MSG_ = "timeout occurred!", TIMEOUT_NS_ = default_spinwait_timeout_ns, ID_ =`gfn, REPORT_FATAL_ = 1) \
  `DV_SPINWAIT_EXIT(WAIT_, `DV_WAIT_TIMEOUT(TIMEOUT_NS_, ID_, MSG_, REPORT_FATAL_);, "", ID_)
`define DV_WAIT(WAIT_COND_, MSG_ = "wait timeout occurred!", TIMEOUT_NS_ = default_spinwait_timeout_ns, ID_ =`gfn, REPORT_FATAL_ = 1) \
  `DV_SPINWAIT(wait (WAIT_COND_);, MSG_, TIMEOUT_NS_, ID_, REPORT_FATAL_)
class c;
  int unsigned default_spinwait_timeout_ns = 10_000_000;
  bit done;
  task t1();
    `DV_SPINWAIT_EXIT(wait (done == 1);, #10ms;, "")
    tw[1] = int'($time);
  endtask
  task t2();
    `DV_SPINWAIT_EXIT(wait (done == 1);, `DV_WAIT_TIMEOUT(default_spinwait_timeout_ns, "id", "m", 1);, "", "id")
    tw[2] = int'($time);
  endtask
  task t3();
    `DV_SPINWAIT(wait (done == 1);)
    tw[3] = int'($time);
  endtask
  task t4();
    `DV_WAIT(done == 1)
    tw[4] = int'($time);
  endtask
endclass
module svforkmacro1;
  initial begin
    automatic c o = new;
    foreach (tw[i]) tw[i] = -1;
    fork o.t1(); o.t2(); o.t3(); o.t4(); join_none
    #50ns o.done = 1;
    #100ns;
    foreach (tw[i]) if (tw[i] != 50) $error("t%0d finished at %0d, want 50", i, tw[i]);
    if (bad_msgs != 0) $error("%0d timeout/exit messages ran", bad_msgs);
    if (tw[1] == 50 && tw[2] == 50 && tw[3] == 50 && tw[4] == 50 && bad_msgs == 0) $display("PASSED");
    else $display("FAILED");
    $finish;
  end
endmodule
