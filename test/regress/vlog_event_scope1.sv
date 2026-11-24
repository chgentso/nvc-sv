package ev_pkg;
   event pe;
endpackage

class ev_cls;
   static event se;
   event me;
   task trigger_self();
      -> this.me;
   endtask
endclass

module vlog_event_scope1;
   int seen_se = 0;
   int seen_pe = 0;
   int seen_me = 0;
   ev_cls c = new;

   initial begin
      @(ev_cls::se);
      seen_se = 1;
   end

   initial begin
      @(ev_pkg::pe);
      seen_pe = 1;
   end

   initial begin
      @(c.me);
      seen_me = 1;
   end

   initial begin
      #1;
      -> ev_cls::se;
      ->> ev_pkg::pe;
      c.trigger_self();
      #2;
      if (seen_se !== 1 || seen_pe !== 1 || seen_me !== 1) begin
         $display("FAIL %0d %0d %0d", seen_se, seen_pe, seen_me);
         $fatal(1);
      end
      $display("PASSED");
      $finish;
   end
endmodule
