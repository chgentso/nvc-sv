typedef int unit_type_t;
localparam int UNIT_P = 42;
int unit_var = 7;

function automatic int unit_inc(int x);
   return x + 1;
endfunction

task automatic unit_add(int v);
   unit_var += v;
endtask

module svunitscope1;
   // Shadow compilation-unit names locally
   typedef logic [7:0] unit_type_t;
   localparam int UNIT_P = 99;
   int unit_var = 100;

   $unit::unit_type_t u;
   unit_type_t local_v;

   initial begin
      if ($bits(u) != 32) $fatal(1, "$unit type width %0d", $bits(u));
      if ($bits(local_v) != 8) $fatal(1, "local type width %0d", $bits(local_v));
      if ($unit::UNIT_P != 42) $fatal(1, "$unit param %0d", $unit::UNIT_P);
      if (UNIT_P != 99) $fatal(1, "local param %0d", UNIT_P);
      if ($unit::unit_inc(5) != 6) $fatal(1, "$unit fn");
      if ($unit::unit_var != 7) $fatal(1, "$unit var %0d", $unit::unit_var);
      $unit::unit_add(3);
      if ($unit::unit_var != 10) $fatal(1, "$unit var after task %0d", $unit::unit_var);
      $unit::unit_var = 20;
      if ($unit::unit_var != 20 || unit_var != 100) $fatal(1, "$unit assign");
      u = $unit::unit_type_t'(123);
      if (u != 123) $fatal(1, "$unit cast %0d", u);

      $display("PASSED");
   end
endmodule
