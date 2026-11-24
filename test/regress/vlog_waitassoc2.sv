// Polled waits on associative arrays with string keys, class members,
// short-circuit guards, key mutation, deletion, clearing and whole-array
// reassignment (IEEE 1800-2017 9.4.3).

interface wa2_ifc;
   logic rst_n;
endinterface

module vlog_waitassoc2;
   wa2_ifc vif();

   typedef struct {
      bit expected;
      string tag;
   } alert_info_t;

   class Cfg;
      bit under_reset = 1;
      Cfg next;
      int val = 0;
   endclass

   class AgentCfg;
      virtual wa2_ifc vif;
   endclass

   class Scoreboard;
      alert_info_t expected_alert[string];
      Cfg cfg;
      AgentCfg agent_cfg;

      task automatic check_alert(string alert_name);
         wait (this.expected_alert[alert_name].expected == 1 &&
               !cfg.under_reset &&
               agent_cfg.vif.rst_n);
         $display("%0t: alert %s woke", $time, alert_name);
      endtask

      task automatic wait_tag(string alert_name);
         wait (expected_alert[alert_name].tag == "go");
         $display("%0t: tag %s woke", $time, alert_name);
      endtask
   endclass

   Scoreboard sb;
   int aa[string];
   int bb[string];
   string dyn_key = "missing";
   string mut_key = "abc";
   int fn_key_idx = 0;
   bit arm = 0;
   logic clk = 0;

   always #1 clk = ~clk;

   function automatic string get_key();
      return (fn_key_idx == 0) ? "k0" : "k1";
   endfunction

   initial begin
      vif.rst_n = 0;
      sb = new;
      sb.cfg = new;
      sb.agent_cfg = new;
      sb.agent_cfg.vif = vif;

      aa["hit"] = 1;
      aa["aXc"] = 7;
      bb["swap"] = 42;

      fork
         // 1. cip_base_scoreboard pattern: string key, missing then added,
         //    guarded class members and vif signal
         sb.check_alert("fatal_fault");

         // 2. string member of assoc struct element
         sb.wait_tag("fatal_fault");

         // 3. string key variable reassigned to existing key
         begin
            wait (aa[dyn_key] == 1);
            $display("%0t: dyn_key woke", $time);
         end

         // 4. string key variable mutated via putc
         begin
            wait (aa[mut_key] == 7);
            $display("%0t: mut_key woke", $time);
         end

         // 5. key deleted -> !exists(k) wakes, then size() == 0 on delete()
         begin
            wait (!aa.exists("hit"));
            $display("%0t: deleted woke", $time);
            wait (aa.size() == 0);
            $display("%0t: cleared woke", $time);
         end

         // 6. whole-array reassignment
         begin
            string k = "swap";
            wait (aa[k] == 42);
            $display("%0t: reassign woke", $time);
         end

         // 7. guarded && with null handle, and || with null handle
         begin
            wait (sb.cfg.next != null && sb.cfg.next.val == 5);
            $display("%0t: guard_and woke", $time);
         end
         begin
            wait (sb.cfg.next != null);
            wait (sb.cfg.next == null || sb.cfg.next.val == 9);
            $display("%0t: guard_or woke", $time);
         end

         // 8. guarded && with unwatchable RHS ($time)
         begin
            wait (arm && $time >= 95);
            $display("%0t: unwatch_rhs woke", $time);
         end

         // 9. unwatchable key expression (function call) falls back to polling
         begin
            wait (aa[get_key()] == 3);
            $display("%0t: fn_key woke", $time);
         end
      join_none

      #10;
      // Add alert with expected=0 first, then expected=1 while still in reset
      sb.expected_alert["fatal_fault"] = '{expected: 0, tag: "init"};
      #10 sb.expected_alert["fatal_fault"].expected = 1;
      #10 sb.cfg.under_reset = 0;         // vif.rst_n still 0
      #10 vif.rst_n = 1;                  // @40: alert fatal_fault wakes

      #5  sb.expected_alert["fatal_fault"].tag = "go"; // @45: tag wakes

      #5  dyn_key = "hit";                // @50: dyn_key wakes
      #5  mut_key.putc(1, "X");           // @55: mut_key wakes

      #5  aa.delete("hit");               // @60: deleted wakes
      #5  aa.delete();                    // @65: cleared wakes
      #5  aa = bb;                        // @70: reassign wakes

      #5  begin                           // @75
            sb.cfg.next = new;
            sb.cfg.next.val = 1;
          end
      #5  sb.cfg.next.val = 5;            // @80: guard_and wakes
      #5  sb.cfg.next.val = 9;            // @85: guard_or wakes

      #5  begin                           // @90: arm && $time>=95 + fn_key
            arm = 1;
            aa["k1"] = 3;
            fn_key_idx = 1;               // @90: fn_key wakes
          end

      #15;                                // @95: unwatch_rhs wakes; end @105
      $display("PASSED");
      $finish;
   end
endmodule
