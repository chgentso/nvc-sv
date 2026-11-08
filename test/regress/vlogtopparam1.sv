// Parameters of a top-level module take the values given with -g on the
// command line, the counterpart of a parameter override for a module that
// is not instantiated (IEEE 1800-2017 23.10, 33.4.1; the means of giving
// the value is tool specific): integer, based number, real and string
// values, and dependent ports and localparams follow the new value.

module vlogtopparam1 #(parameter WIDTH = 4,
                       parameter logic [7:0] B = 8'h00,
                       parameter real R = 0.0,
                       parameter string S = "default",
                       parameter int UNSET = 3)
   (input [WIDTH-1:0] data);

   localparam int W2 = WIDTH * 2;

   initial begin
      if (WIDTH != 12) $fatal(1, "WIDTH = %0d", WIDTH);
      if ($bits(data) != 12) $fatal(1, "$bits(data) = %0d", $bits(data));
      if (W2 != 24) $fatal(1, "W2 = %0d", W2);
      if (B !== 8'ha5) $fatal(1, "B = %h", B);
      if (R != 2.5) $fatal(1, "R = %f", R);
      if (S != "hello") $fatal(1, "S = %s", S);
      if (UNSET != 3) $fatal(1, "UNSET = %0d", UNSET);
      $display("PASSED");
   end
endmodule
