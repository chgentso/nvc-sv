// wait and @ on state which is not a signal
// Expected values: IEEE 1800-2017 9.4.3 (wait is level sensitive: it resumes once the condition
// becomes true), 9.4.2 (event controls: @ on class properties, posedge/negedge/edge), 6.8.
// Wake-up order of processes released at the same time is not checked (4.7): every event is
// recorded with its time and the set of (time, text) pairs is compared at the end.
`timescale 1ns/1ns
string log[$];
int errors = 0;
function automatic void rec(string t);
   log.push_back($sformatf("%0d %s", $time, t));
endfunction
function automatic void expect_ev(int t, string txt);
   string k = $sformatf("%0d %s", t, txt);
   int n = 0;
   foreach (log[i]) if (log[i] == k) n++;
   if (n != 1) begin $error("event '%s' seen %0d times, exp 1", k, n); errors++; end
endfunction
class flags;
   int count;
   bit done;
   logic clk = 0;
   int q[$];
   event ev;

   task automatic wait_done(string who);
      wait (done);
      rec($sformatf("%s saw done", who));
   endtask

   task automatic wait_this(int n);
      wait (this.count >= n);
      rec($sformatf("count reached %0d", n));
   endtask

   function bit is_big();
      return count > 3;
   endfunction

   function int size();
      return q.size();
   endfunction
endclass

module svwait1;
   flags f = new;
   int q[$];
   string s = "";
   int sig = 0;
   int nba_seen = 0;

   // Never true: must not stop the simulation from finishing
   initial begin
      wait (f.count == 12345);
      rec("impossible condition");
   end

   initial begin
      wait (q.size() > 0);
      rec($sformatf("queue has %0d", q.size()));
      wait (q.size() == 0);
      rec("queue is empty");
   end

   initial begin
      wait (f.is_big());
      rec("is_big");
   end

   initial begin
      wait (s == "go");
      rec("string is go");
   end

   // Condition mixing a signal and class state
   initial begin
      wait (sig == 2 && f.count == 7);
      rec("signal and class state");
   end

   initial begin
      automatic int local_count = 0;
      fork
         #15 local_count = 3;
         begin
            wait (local_count == 3);
            rec("local variable");
         end
      join
   end

   initial fork
      f.wait_done("a");
      f.wait_done("b");
      f.wait_this(2);
      f.wait_this(5);
   join

   // Event controls on class state
   initial begin
      @(f.count);
      rec($sformatf("count changed to %0d", f.count));
      @(f.count or f.done);
      rec("count or done changed");
   end

   initial repeat (2) begin
      @(posedge f.clk);
      rec("posedge");
   end

   initial repeat (2) begin
      @(negedge f.clk);
      rec("negedge");
   end

   initial repeat (3) begin
      @(edge f.clk);
      rec("edge");
   end

   // Method call on an object
   initial begin
      wait (f.size() == 1);
      rec("size is one");
      f.q.delete();
   end

   always @(f.done) nba_seen++;

   initial begin
      #1 f.q.push_back(9);
      #9 f.count = 1;
      #10 q.push_back(5);
      #5 f.count = 2;
      #5 s = "go";
      #5 f.clk = 1;
      #5 f.clk = 0;
      #5 f.clk = 1;
      #5 f.clk = 0;
      #5 f.count = 4;
      #5 q.delete();
      #5 f.count = 5;
      #5 sig = 2;
      #5 f.q.push_back(1);
      f.q.push_back(2);
      #5 f.done = 1;
      #5 f.done = 0;
      #5 f.count = 7;
      #5 f.q.delete();
      #5;
      if (nba_seen != 2) begin $error("nba_seen %0d exp 2", nba_seen); errors++; end
      expect_ev(1, "size is one");
      expect_ev(10, "count changed to 1");
      expect_ev(15, "local variable");
      expect_ev(20, "queue has 1");
      expect_ev(25, "count reached 2");
      expect_ev(25, "count or done changed");
      expect_ev(30, "string is go");
      expect_ev(35, "posedge");
      expect_ev(35, "edge");
      expect_ev(40, "negedge");
      expect_ev(40, "edge");
      expect_ev(45, "posedge");
      expect_ev(45, "edge");
      expect_ev(50, "negedge");
      expect_ev(55, "is_big");
      expect_ev(60, "queue is empty");
      expect_ev(65, "count reached 5");
      expect_ev(80, "a saw done");
      expect_ev(80, "b saw done");
      expect_ev(90, "signal and class state");
      if (log.size() != 20) begin $error("%0d events, exp 20", log.size()); errors++; end
      if (errors == 0) $display("PASSED");
   end
endmodule
