// A process forked with join_none that assigns an element, slice or field
// of a local variable shares that variable with the parent and siblings
module svforkelem1;
   typedef struct packed { bit [3:0] hi; bit [3:0] lo; } pair_t;

   class cls;
      int f;
   endclass

   task automatic run();
      bit [0:0][0:0] flags;
      bit [7:0] vec;
      int arr[2];
      pair_t pr;
      cls h = new;
      int seen = 0;

      fork
         begin
            #10;
            flags[0] = 1;
            vec[3:0] = 4'ha;
            arr[1] = 42;
            pr.lo = 4'h5;
            h.f = 7;
         end
      join_none

      fork
         begin
            forever begin
               #1;
               if (flags[0][0]) begin
                  seen = 1;
                  break;
               end
            end
         end
      join_none

      #20;
      if (flags[0] !== 1'b1) $fatal(1, "flags %b", flags);
      if (vec !== 8'h0a) $fatal(1, "vec %h", vec);
      if (arr[1] !== 42) $fatal(1, "arr %0d", arr[1]);
      if (pr.lo !== 4'h5) $fatal(1, "pr %h", pr);
      if (h.f !== 7) $fatal(1, "h.f %0d", h.f);
      if (seen !== 1) $fatal(1, "sibling did not see the update");
   endtask

   initial begin
      run();
      $display("PASSED");
   end
endmodule
