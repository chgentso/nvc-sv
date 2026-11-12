// A cast between a vector and an unpacked structure is not supported
module svstructcast1;
  typedef struct { logic [6:0] lo; logic [7:0] hi; } s15_t;
  s15_t x;
  initial begin
    x = s15_t'(15'h2bcd);
    $display("not reached");
  end
endmodule
