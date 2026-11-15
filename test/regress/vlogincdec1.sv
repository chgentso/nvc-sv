// The index of the target of an increment or decrement is evaluated once
// (IEEE 1800-2017 11.4.2).

module vlogincdec1;
   class C;
      int idx = 5;
      int n = 0;
      function int get_index();
         n++;
         return idx;
      endfunction
   endclass

   C cls = new;
   int array[10];
   int r;

   initial begin
      array[5] = 50;
      array[cls.get_index()]++;
      if (array[5] !== 51 || cls.n != 1)
         $fatal(1, "postfix %0d n=%0d", array[5], cls.n);
      --array[cls.get_index()];
      if (array[5] !== 50 || cls.n != 2)
         $fatal(1, "prefix %0d n=%0d", array[5], cls.n);
      r = array[cls.get_index()]++;
      if (r !== 50 || array[5] !== 51 || cls.n != 3)
         $fatal(1, "value %0d %0d n=%0d", r, array[5], cls.n);
      r = ++array[cls.get_index()];
      if (r !== 52 || array[5] !== 52 || cls.n != 4)
         $fatal(1, "value %0d %0d n=%0d", r, array[5], cls.n);
      $display("PASSED");
      $finish;
   end
endmodule
