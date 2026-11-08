// Expected values: IEEE 1800-2017 9.3.2 (join_any), 9.6.1 (disable fork kills the children of the
// calling process), 9.4.3 (wait). `done` toggles every 1 ns, so the 10 ms timeout can never win and each
// loop iteration takes between 1 and 2 ns: after 20001 ns between 10000 and 20000 iterations are done.
`timescale 1ns/1ns
class c;
  bit done;
  int iters = 0;
  int timeouts = 0;
  task t();
    repeat (20000) begin
      fork begin
        fork
          begin wait (done == 1); end
          begin #10ms; timeouts++; end
        join_any
        disable fork;
      end join
      iters++;
      #1ns;
    end
  endtask
endclass
module svforkleak1;
  initial begin
    automatic c o = new;
    fork o.t(); join_none
    fork repeat (20000) begin #1ns o.done = ~o.done; end join_none
    #20001ns;
    if (o.timeouts != 0) $error("timeout branch ran %0d times", o.timeouts);
    if (o.iters < 10000 || o.iters > 20000) $error("iterations %0d outside 10000..20000", o.iters);
    if (o.timeouts == 0 && o.iters >= 10000 && o.iters <= 20000) $display("PASSED");
    else $display("FAILED");
    $finish;
  end
endmodule
