module inval2;
   parameter integer c = 0;
   logic [7:0] a, b;
   initial b[4+:c] = a[1+:c];
endmodule
