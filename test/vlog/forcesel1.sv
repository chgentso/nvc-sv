// IEEE 1800-2017 10.6.2: the target of a force or release cannot be a bit
// or part select with a variable index.
module forcesel1;
  wire array1[2:1];
  wire [2:-1] vec;
  integer bad_index = 1;
  parameter P_ONE = 1;
  initial begin
    force array1[P_ONE] = 1'b1;                 // OK
    release array1[P_ONE];                      // OK
    force vec[P_ONE+:1] = 1'b1;                 // OK
    release vec[P_ONE+:1];                      // OK
    force array1[bad_index] = 1'b1;             // Error
    release array1[bad_index];                  // Error
    force vec[bad_index+:1] = 1'b1;             // Error
    release vec[bad_index+:1];                  // Error
  end
endmodule
