// IEEE 1800-2017 11.5.1: a bit or part select needs a vector and a variable
// has no more indices than it has dimensions.
module selcount1(input unk, input [3:0] vec, output out);
  wire b;
  wire [0:0] one;
  logic [1:0][3:0] m;
  logic [7:0] mem [3:0];
  int n;
  assign out = b[0];                            // Error
  wire [1:0] w = unk[3:2];                      // Error
  wire x = m[0][1][0];                          // Error
  wire y = mem[1][2][3];                        // Error
  wire ok1 = one[0];                            // OK
  wire ok2 = vec[1];                            // OK
  wire ok3 = m[1][2];                           // OK
  wire ok4 = mem[1][2];                         // OK
  wire ok5 = n[3];                              // OK
  wire [1:0] ok6 = vec[2:1];                    // OK
endmodule
