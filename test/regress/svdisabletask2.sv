// Disabling a task by name while another process is suspended in it on a
// wait or event control which reads memory: the wait is polled and the
// task must still be interrupted after the poll has been repeated
interface svdisabletask2_if;
   logic en = 1'b0;
endinterface

module svdisabletask2;
   svdisabletask2_if bif();

   class cfg_c;
      virtual svdisabletask2_if bif;
   endclass

   class drv;
      cfg_c cfg;
      bit   flag;
      bit   timed_out;
      int   reached;
      int   r;

      task get_vif_wait();
         wait (cfg.bif.en);
         reached++;
         r = 1;
      endtask

      task get_flag_wait();
         wait (flag);
         reached++;
         r = 2;
      endtask

      task get_event();
         @(posedge cfg.bif.en);
         reached++;
         r = 3;
      endtask

      task run_vif();
         fork begin : isolation_fork
            fork
               begin
                  repeat (4) #10;
                  timed_out = 1;
                  disable get_vif_wait;
               end
               get_vif_wait();
            join
         end join
      endtask

      task run_flag();
         fork begin : isolation_fork
            fork
               begin
                  repeat (4) #10;
                  disable get_flag_wait;
               end
               get_flag_wait();
            join
         end join
      endtask

      task run_event();
         fork begin : isolation_fork
            fork
               begin
                  repeat (4) #10;
                  disable get_event;
               end
               get_event();
            join
         end join
      endtask
   endclass

   initial begin
      automatic drv d = new;
      d.cfg = new;
      d.cfg.bif = bif;

      d.run_vif();
      if ($time != 40 || !d.timed_out || d.reached != 0)
         $fatal(1, "vif wait not disabled at %0t", $time);
      d.run_flag();
      if ($time != 80 || d.reached != 0)
         $fatal(1, "flag wait not disabled at %0t", $time);
      d.run_event();
      if ($time != 120 || d.reached != 0)
         $fatal(1, "event not disabled at %0t", $time);
      if (d.r != 0) $fatal(1, "tasks completed");
      $display("PASSED");
   end
endmodule
