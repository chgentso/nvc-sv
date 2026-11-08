// random_qualifier on a member of a structure

module svrandstruct1;
  typedef struct {
    rand bit [15:0] data;
    randc bit [3:0] c;
    bit [7:0] k;
  } adc_channel_t;
  adc_channel_t c;
  initial begin
    c.data = 16'h1234;
    c.k = 8'h56;
    if (c.data !== 16'h1234 || c.k !== 8'h56) $display("FAILED");
    else $display("PASSED");
  end
endmodule
