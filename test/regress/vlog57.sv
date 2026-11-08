// Dynamic arrays and queues
typedef int unsigned u32;
typedef u32 u32_q[$];

module vlog57;
   initial begin
      u32 arr[], copy[], val;
      u32_q q, r;
      int s;
      arr = new[3];
      arr[1] = 5;
      if (arr.size() != 3 || arr[1] != 5) $display("FAILED dyn");
      copy = arr;
      copy[1] = 6;
      if (arr[1] != 5) $display("FAILED copy");
      q[0] = 1;
      q[1] = 2;
      q.push_back(3);
      q.push_front(0);
      if (q.size() != 4) $display("FAILED size %0d", q.size());
      if (q[$] != 3) $display("FAILED last");
      val = q.pop_front();
      if (val != 0 || q.size() != 3) $display("FAILED pop front");
      val = q.pop_back();
      if (val != 3 || q.size() != 2) $display("FAILED pop back");
      r = q;
      r[0] = 9;
      if (q[0] != 1) $display("FAILED queue copy");
      q = {};
      if (q.size() != 0) $display("FAILED empty");
      r.delete();
      if (r.size() != 0) $display("FAILED delete");
      $display("PASSED");
   end
endmodule
