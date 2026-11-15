// IEEE 1800-2017 23.3.2.3: an implicit .name port connection needs a signal
// of that name and does not declare an implicit net.
module dotname1_sub(output o, input i);
endmodule
module dotname1;
  wire i;
  dotname1_sub u1(.o, .i);                      // Error
  wire o;
  dotname1_sub u2(.o, .i);                      // OK
endmodule
