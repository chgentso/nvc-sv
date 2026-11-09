// Conditions of wait statements that read class members, locals shared
// with forked processes, queue lengths and virtual interface signals are
// re-evaluated when the memory they read changes (IEEE 1800-2017 9.4.3).

interface ifc(input logic clk);
   logic rst_n;
   logic [3:0] data;
endinterface

module svpollwatch1;
   logic clk = 0;
   ifc vif(clk);

   class Cfg;
      int state = 0;
      bit flag = 0;
      int q[$];
      string name = "";
      virtual ifc v;
      Cfg next;
   endclass

   Cfg cfg;
   int log_n = 0;
   int seen[10];

   task automatic note(int id);
      seen[log_n++] = id;
      $display("%0t id=%0d", $time, id);
   endtask

   // Waits on a member written by another process in the same time step
   task automatic w_member();
      wait (cfg.state == 3);
      note(1);
   endtask

   // Waits on a local of the parent shared with a forked process
   task automatic w_local();
      int local_v = 0;
      fork
         begin #7; local_v = 5; end
         begin wait (local_v == 5); note(2); end
      join
   endtask

   // Waits on queue length
   task automatic w_queue();
      wait (cfg.q.size() == 2);
      note(3);
   endtask

   // Virtual interface signal
   task automatic w_vif();
      wait (cfg.v.rst_n === 1'b1);
      note(4);
   endtask

   // Short-circuit: the right operand is a member of a handle which is null
   task automatic w_guard();
      wait (cfg.next != null && cfg.next.state == 9);
      note(5);
   endtask

   // Bit of a member and a conjunction
   task automatic w_flag();
      wait (cfg.flag && cfg.state >= 3);
      note(6);
   endtask

   // Handle reassigned
   task automatic w_handle();
      Cfg c = cfg;
      wait (c.next != null);
      note(7);
   endtask

   // String length
   task automatic w_str();
      wait (cfg.name.len() > 2);
      note(8);
   endtask

   // Vector signal slice via the interface
   task automatic w_data();
      wait (cfg.v.data == 4'h9);
      note(9);
   endtask

   initial begin
      cfg = new;
      cfg.v = vif;
      vif.rst_n = 0;
      vif.data = 0;
      fork
         w_member();
         w_local();
         w_queue();
         w_vif();
         w_guard();
         w_flag();
         w_handle();
         w_str();
         w_data();
      join_none

      #2 cfg.state = 1;
      #1 cfg.q.push_back(1);
      #1 cfg.flag = 1;
      #1 cfg.state = 3;
      #1 cfg.q.push_back(2);
      #1 vif.rst_n = 1;
      #1 cfg.name = "ab";
      #1 cfg.name = "abc";
      #1 vif.data = 4'h9;
      #1 begin
         cfg.next = new;
         cfg.next.state = 1;
      end
      #1 cfg.next.state = 9;
      #5;
      if (log_n != 9) $fatal(1, "only %0d woke", log_n);
      $display("PASSED");
   end
endmodule
