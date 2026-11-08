set -xe

pwd
which nvc

# A header with no include guard can define the same unit twice

cat >hdr.svh <<EOT
interface vlog70_if;
   logic [3:0] x = 4'h3;
   function automatic logic [3:0] get();
      return x;
   endfunction
endinterface
EOT

cat >top.sv <<EOT
\`include "hdr.svh"
module sub;
   vlog70_if i ();
   function automatic logic [3:0] get();
      return i.get();
   endfunction
endmodule
\`include "hdr.svh"
module top;
   sub s ();
   vlog70_if i ();
   initial begin
      #1;
      if (i.get() !== 4'h3) \$display("FAILED %x", i.get());
      if (s.get() !== 4'h3) \$display("FAILED %x", s.get());
      \$display("PASSED");
      \$finish;
   end
endmodule
EOT

nvc -a top.sv 2>&1 | tee ana
! grep "replaces a previously analysed" ana
nvc -e top
nvc -r top 2>&1 | tee out
grep PASSED out
! grep FAILED out
