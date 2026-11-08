// Dynamic types, tasks with ref, events, mailbox, fork
typedef int unsigned u32;
typedef u32 u32_q[$];
typedef struct {
   u32 a;
   int size;
} xfer_t;
typedef struct {
   int rw;
   xfer_t data[$];
   event done;
} cycle_t;

class msg_t;
   u32_q data;
   string name;
   function new(string name = "m", u32_q d = {});
      this.name = name;
      data = d;
   endfunction
   function int size();
      return data.size();
   endfunction
endclass

module svdyn1;
   mailbox #(cycle_t) q;
   mailbox #(cycle_t) q2;
   int fails = 0;
   u32 total = 0;

   task automatic set_ref(ref u32 v, input u32 x);
      #1ns;
      v = x;
   endtask

   task automatic worker();
      forever begin
         cycle_t c;
         q.get(c);
         #5ns;
         for (int i = 0; i < c.data.size(); i++)
            c.data[i].a = c.data[i].a + 1;
         ->c.done;
         q2.put(c);
      end
   endtask

   task automatic transact(u32 x, output u32 r);
      cycle_t c;
      xfer_t f;
      f.a = x;
      f.size = 4;
      c.rw = 1;
      c.data.push_back(f);
      q.put(c);
      @c.done;
      q2.get(c);
      r = c.data[0].a;
   endtask

   function automatic string fmt(int n);
      return $sformatf("n=%0d", n);
   endfunction

   initial begin
      u32 val, arr[], hdr[$];
      msg_t m;
      string s;
      q = new();
      q2 = new();
      fork
         worker();
      join_none
      set_ref(val, 42);
      if (val != 42) begin $display("FAILED ref %0d", val); fails++; end
      transact(10, val);
      if (val != 11) begin $display("FAILED mailbox %0d", val); fails++; end
      arr = new[3];
      arr[1] = 5;
      if (arr.size() != 3 || arr[1] != 5) begin $display("FAILED dyn"); fails++; end
      hdr[0] = 1;
      hdr[1] = 2;
      hdr[2] = 3;
      if (hdr.size() != 3) begin $display("FAILED q ext %0d", hdr.size()); fails++; end
      if (hdr[$] != 3) begin $display("FAILED q last"); fails++; end
      val = hdr.pop_front();
      if (val != 1 || hdr.size() != 2) begin $display("FAILED popf"); fails++; end
      val = hdr.pop_back();
      if (val != 3 || hdr.size() != 1) begin $display("FAILED popb"); fails++; end
      hdr = {};
      if (hdr.size() != 0) begin $display("FAILED empty"); fails++; end
      m = new("hello");
      m.data.push_back(7);
      if (m.size() != 1 || m.data[0] != 7) begin $display("FAILED m"); fails++; end
      s = {m.name, " ", fmt(3)};
      if (s != "hello n=3") begin $display("FAILED str <%s>", s); fails++; end
      $display("str is %s and %0s %0d", s, m.name, m.size());
      for (int i = 0; i < 5; i++) begin
         if (i == 1) continue;
         if (i == 3) break;
         total += i;
      end
      if (total != 2) begin $display("FAILED loop %0d", total); fails++; end
      if (fails == 0) $display("PASSED");
      $finish;
   end
endmodule
