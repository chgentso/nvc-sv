// Expected values: IEEE 1800-2017 27.5/27.4 (generate blocks and nested generate blocks), 9.2,
// 9.4.2, 16.14.1 (concurrent assertion pass action runs on a non-vacuous success), 22.7 timescale.
// clk rises at 6, simulation ends at 9. Each generate-scope process runs once at its delay and
// sees d == 7, w == 4'ha; the always block and the assertion pass action run once, at 6.
`timescale 1ns/1ns
module svgenvarsystf1;
  logic clk = 0;
  int d = 7;
  logic [3:0] w = 4'ha;
  int ninit = 0, ninner = 0, nalw = 0, npass = 0, nloop = 0, bad = 0;
  int loop_t[2];
  if (1) begin : chk
    initial #1 begin
      ninit++;
      if (d != 7 || w !== 4'ha || $time != 1) begin $error("init"); bad++; end
    end
    always @(posedge clk) begin
      nalw++;
      if (d != 7 || w !== 4'b1010 || $time != 6) begin $error("always"); bad++; end
    end
    assert property (@(posedge clk) d == 7) begin
      npass++;
      if (d != 7 || $time != 6) begin $error("pass action"); bad++; end
    end else ;
    if (1) begin : inner
      initial #2 begin
        ninner++;
        if (d != 7 || w !== 4'ha || $time != 2) begin $error("inner"); bad++; end
      end
    end
  end
  for (genvar k = 0; k < 2; k++) begin : lp
    initial #(3 + k) begin
      nloop++;
      loop_t[k] = int'($time);
      if (d != 7) begin $error("loop"); bad++; end
    end
  end
  initial begin
    #6 clk = 1; #3;
    if (ninit != 1 || ninner != 1 || nalw != 1 || npass != 1 || nloop != 2 || loop_t[0] != 3 || loop_t[1] != 4 || bad != 0) begin
      $error("counts init=%0d inner=%0d always=%0d pass=%0d loop=%0d t=%0d,%0d bad=%0d",
             ninit, ninner, nalw, npass, nloop, loop_t[0], loop_t[1], bad);
      $display("FAILED");
    end else $display("PASSED");
    $finish;
  end
endmodule
