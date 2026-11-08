// Events, including inside structures which share them when copied
typedef struct {
   int   v;
   event done;
} req_t;

class holder;
   event e;
   int count = 0;
   task automatic wait_it();
      @e;
      count++;
   endtask
endclass

module vlog64;
   mailbox #(req_t) q;
   req_t pending;
   int got = 0, tries;
   int ok = 0;

   initial begin
      req_t r;
      q = new();
      r.v = 5;
      if (q.num() != 0) $display("FAILED num");
      tries = q.try_get(r);
      if (tries != 0) $display("FAILED try_get empty");
      r.v = 5;
      q.put(r);
      if (q.num() != 1) $display("FAILED put");
      tries = q.try_get(pending);
      if (tries != 1 || pending.v != 5) $display("FAILED try_get");
      q.put(r);
      q.get(pending);
      if (pending.v != 5) $display("FAILED get");
      $display("PASSED");
   end
endmodule
