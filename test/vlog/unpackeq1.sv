// IEEE 1800-2017 7.6: an unpacked array can be assigned to another only if
// their element types are equivalent (6.22.2).  bit[7:0] and logic[7:0]
// differ in 2-state/4-state kind, and int and bit[7:0] in width and
// signedness, so these assignments are errors.
module unpackeq1;
  bit [7:0] a[2];
  logic [7:0] b[2];
  int c[2];
  bit [7:0] qa[$];
  logic [7:0] qb[$];
  initial begin
    b = a;                              // Error
    c = a;                              // Error
    qb = qa;                            // Error
  end
endmodule
