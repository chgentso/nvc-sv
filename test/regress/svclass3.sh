set -xe

pwd
which nvc

# Elaborating and running as separate commands

cat >top.sv <<EOT
module top;
   class b;
      virtual function int f();
         return 1;
      endfunction
   endclass

   class d extends b;
      rand int x;
      constraint k { x inside {[5:7]}; }

      virtual function int f();
         return 2;
      endfunction
   endclass

   initial begin
      automatic d o = new;
      automatic b h = o;
      if (h.f() != 2) \$display("FAILED virtual");
      if (h.randomize() != 1) \$display("FAILED randomize");
      if (o.x < 5 || o.x > 7) \$display("FAILED x");
      \$display("PASSED");
      \$finish;
   end
endmodule
EOT

nvc -a top.sv
nvc -e top
nvc -r top 2>&1 | tee out
grep PASSED out
! grep FAILED out
