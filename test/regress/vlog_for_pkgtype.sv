package pk;
  typedef enum int {A = 0, B = 1, C = 2} e_t;
  typedef logic [3:0] nib_t;
endpackage

module vlog_for_pkgtype;
  initial begin
    int n = 0;
    for (pk::e_t g = pk::A; g <= pk::C; g++) n += g + 1;
    for (pk::nib_t k = 0, j = 3; k < j; k++) n += 10;
    if (n == 36) $display("PASSED");
    else $display("FAILED %0d", n);
  end
endmodule
