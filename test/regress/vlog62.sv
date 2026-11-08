// Arguments by reference, default values, and void functions
module vlog62;
   int calls = 0;

   task automatic set_ref(ref int v, input int x = 42);
      #1;
      v = x;
   endtask

   function automatic void note();
      calls++;
      return;
   endfunction

   function automatic int sum(int a, int b = 10);
      return a + b;
   endfunction

   task automatic grow(ref int q[$]);
      q.push_back(q.size());
   endtask

   initial begin
      int v, q[$];
      set_ref(v);
      if (v != 42) $display("FAILED ref %0d", v);
      set_ref(v, 1);
      if (v != 1) $display("FAILED value");
      note();
      if (calls != 1) $display("FAILED void");
      if (sum(1) != 11 || sum(1, 2) != 3) $display("FAILED default");
      grow(q);
      grow(q);
      if (q.size() != 2 || q[1] != 1) $display("FAILED queue ref");
      $display("PASSED");
   end
endmodule
