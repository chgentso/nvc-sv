program prog(input wire a, input wire [3:0] b);
  initial #1 begin
    if (a !== 1'b1 || b !== 4'd9) $fatal(1, "prog %0d %0d", a, b);
    $display("PASSED");
  end
endprogram

module svmisc3;
  specparam [3:0] w = 5, delay = 50;
  typedef int triple [0:2];
  triple t = '{0:1, default:0};
  triple u = '{7, 8, 9};
  wire a = 1;
  wire [3:0] b = 4'd9;

  prog p(a, b);

  initial begin
    parameter [3:0] val0 = 4'b000x;
    parameter [3:0] val1 = 4'b0001;
    localparam q = 3;
    if (w !== 5 || delay !== 2) $fatal(1, "specparam %0d %0d", w, delay);
    if ($isunknown(val0) !== 1 || $isunknown(val1) !== 0 || q !== 3)
      $fatal(1, "block parameters");
    if (t[0] !== 1 || t[1] !== 0 || t[2] !== 0) $fatal(1, "keyed pattern");
    if (u[0] !== 7 || u[1] !== 8 || u[2] !== 9) $fatal(1, "positional pattern");
    t = '{2:5, 1:6, default:2};
    if (t[0] !== 2 || t[1] !== 6 || t[2] !== 5) $fatal(1, "keyed pattern 2");
  end
endmodule
