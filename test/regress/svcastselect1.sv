module svcastselect1;
  typedef enum { A, B, C } abc_t;
  typedef struct packed {
    logic [7:0] hi;
    logic [7:0] lo;
  } pair_t;

  int i;
  initial begin
    i = 1;
    if (abc_t'(i).name() != "B") $fatal(1, "name=%s", abc_t'(i).name());
    if (abc_t'(i).next().name() != "C") $fatal(1, "next=%s", abc_t'(i).next().name());
    if (pair_t'(16'h1234).hi !== 8'h12) $fatal(1, "hi=%h", pair_t'(16'h1234).hi);
    if (pair_t'(16'h1234).lo !== 8'h34) $fatal(1, "lo=%h", pair_t'(16'h1234).lo);
    $display("PASSED");
    $finish;
  end
endmodule
