package svsizeenum1_pkg;
  typedef enum logic [3:0] {
    MuFalse = 4'h9,
    MuTrue  = 4'h6
  } mubi4_t;

  typedef struct packed {
    logic [7:0]  top;
    mubi4_t      instr_type;
    logic [13:0] low;
  } user_t;

  function automatic int get_lsb();
    user_t u = '0;
    u.instr_type = mubi4_t'(1);
    return $clog2(u);
  endfunction

  function automatic int get_msb();
    user_t u = '0;
    return get_lsb() + $size(u.instr_type) - 1;
  endfunction

  parameter int LsbPos = get_lsb();
  parameter int MsbPos = get_msb();
endpackage

module svsizeenum1;
  import svsizeenum1_pkg::*;

  logic [25:0] bits = {8'hAA, MuTrue, 14'h1234};

  initial begin
    if (LsbPos !== 14 || MsbPos !== 17)
      $fatal(1, "FAIL: LsbPos=%0d MsbPos=%0d", LsbPos, MsbPos);
    if (bits[MsbPos:LsbPos] !== MuTrue)
      $fatal(1, "FAIL: slice=%h", bits[MsbPos:LsbPos]);
    $display("PASSED");
  end
endmodule
