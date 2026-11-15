// IEEE 1800-2017 23.3.2.4: an instance has at most one .* connection.
module dotstar2_sub(input value);
endmodule
module dotstar2;
  wire value = 1'b0;
  dotstar2_sub u1(.*, .*);                      // Error
  dotstar2_sub u2(.*);                          // OK
endmodule
