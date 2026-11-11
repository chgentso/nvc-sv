// A ref argument used by a process forked in a task refers to the actual
// argument whether it is a module variable or a local of the caller
module svforkref1;
   class item;
      int v;
   endclass

   int g_int;
   item g_item;

   task automatic leaf_int(ref int r);
      #5 r = r + 7;
   endtask

   task automatic leaf_item(ref item r);
      #5 r = new;
      r.v = 9;
   endtask

   task automatic with_fork(ref int r);
      fork
         leaf_int(r);
      join
   endtask

   task automatic with_fork_any(ref int r);
      fork
         leaf_int(r);
         #50;
      join_any
   endtask

   task automatic with_fork_item(ref item r);
      fork
         leaf_item(r);
      join
   endtask

   task automatic with_fork_read(ref int r, output int seen);
      fork
         #5 seen = r;
         #1 r = 20;
      join
   endtask

   initial begin
      automatic int loc = 1;
      automatic item it;
      automatic int seen;

      g_int = 1;
      with_fork(g_int);
      if (g_int != 8) $fatal(1, "module variable %0d", g_int);

      with_fork(loc);
      if (loc != 8) $fatal(1, "local %0d", loc);

      with_fork_any(loc);
      if (loc != 15) $fatal(1, "join_any local %0d", loc);

      with_fork_item(g_item);
      if (g_item == null || g_item.v != 9) $fatal(1, "module handle");

      with_fork_item(it);
      if (it == null || it.v != 9) $fatal(1, "local handle");

      with_fork_read(g_int, seen);
      if (seen != 20 || g_int != 20) $fatal(1, "read %0d %0d", seen, g_int);

      $display("PASSED");
   end
endmodule
