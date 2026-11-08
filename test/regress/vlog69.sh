set -xe

pwd
which nvc

cat >top.sv <<EOT
// Functions must not block
module vlog69;
   class c;
      function new();
         fork               // Error
            #1;
         join_any
      endfunction
   endclass

   function automatic void f1();
      fork                  // Error
         #1;
      join
   endfunction

   function automatic void f2();
      fork                  // Error
         #1;
      join_any
   endfunction

   function automatic void f3();
      wait fork;            // Error
   endfunction

   function automatic void f4();
      fork                  // OK
         begin
            #1;
            wait fork;      // OK: runs in its own process
         end
      join_none
      disable fork;         // OK
   endfunction

   task automatic t1();
      fork                  // OK
         #1;
      join
      wait fork;            // OK
   endtask
endmodule
EOT

! nvc -a top.sv 2>&1 | tee out

# Exactly the four marked statements are rejected
[ $(grep -c "must not block" out) = 4 ]
grep "top.sv:5" out
grep "top.sv:12" out
grep "top.sv:18" out
grep "top.sv:24" out
