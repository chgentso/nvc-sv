// Expected values: IEEE 1800-2017 20.5 ($rtoi truncates toward zero, result integer), 20.8.1 ($clog2),
// 20.9 ($countones), 20.6.2 ($bits), 11.4.10 (>> is logical), 6.24.1 ($signed/$unsigned), 11.3 (arithmetic)
// Left out: $rtoi of values outside the 32-bit range (not defined by the LRM).
module svrtoi1;
  int x; real r = -2.6;
  localparam int LP = $clog2(9) - 10;
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  integer big;
  initial begin
    chk($rtoi(r) + 1 == -1, "rtoi+1");
    x = 3;
    chk(($rtoi(r) < 0) == 1, "rtoi<0");
    chk($rtoi(r) == -2, "rtoi var");
    chk($rtoi(-2.6) == -2, "rtoi neg const");
    chk($rtoi(2.6) == 2, "rtoi pos const");
    chk($clog2(x) - 10 == -8, "clog2(3)");
    chk($clog2(5) - 10 == -7, "clog2(5)");
    chk(LP == -6, "localparam clog2(9)");
    chk($countones(x) - 10 == -8, "countones");
    chk($bits(x) - 40 == -8, "bits");
    big = $rtoi(1e10); big = $rtoi(-1e10);       // out of range: result not checked
    chk($rtoi(r) * 1000000 == -2000000, "mul");
    chk($rtoi(r) / 2 == -1, "div");
    chk(($rtoi(r) >> 30) == 3, "logical shift of 32'hfffffffe");
    chk($rtoi(2.9) + $rtoi(-2.9) == 0, "sym trunc");
    chk($clog2(0) - 1 == -1, "clog2(0)");
    chk($signed($clog2(1024)) - 11 == -1, "signed clog2");
    chk($unsigned($rtoi(r)) == 32'd4294967294, "unsigned");
    chk($rtoi(r) == -2, "== -2");
    chk($rtoi(r) > -3, "> -3");
    if (err == 0) $display("PASSED");
  end
endmodule
