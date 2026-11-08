class c;
   int x;
endclass

module nba1;
   task automatic t(ref int r);
      int a;
      int q[$];
      c   h;
      a <= 1;                   // Error
      q[0] <= 1;                // Error
      r <= 1;                   // Error
      h.x <= 1;                 // OK
   endtask
endmodule
