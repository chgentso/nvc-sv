module svarrayquery1;
  logic [5:2] [7:4] a [1:3];
  typedef logic [7:0] byte_t;
  typedef byte_t arr_t [5:10];
  int d;

  initial begin
    for (d = 0; d <= 4; d++) begin
      case (d)
        1: begin
          if ($left(a, d) !== 1 || $right(a, d) !== 3 || $size(a, d) !== 3
              || $low(a, d) !== 1 || $high(a, d) !== 3 || $increment(a, d) !== -1)
            $fatal(1, "dim 1 mismatch");
          if ($left(arr_t, d) !== 5 || $right(arr_t, d) !== 10 || $size(arr_t, d) !== 6)
            $fatal(1, "arr_t dim 1 mismatch");
          if ($left(a[1][4:2], d) !== 4 || $right(a[1][4:2], d) !== 2 || $size(a[1][4:2], d) !== 3)
            $fatal(1, "slice dim 1 mismatch");
        end
        2: begin
          if ($left(a, d) !== 5 || $right(a, d) !== 2 || $size(a, d) !== 4
              || $low(a, d) !== 2 || $high(a, d) !== 5 || $increment(a, d) !== 1)
            $fatal(1, "dim 2 mismatch");
          if ($left(arr_t, d) !== 7 || $right(arr_t, d) !== 0 || $size(arr_t, d) !== 8)
            $fatal(1, "arr_t dim 2 mismatch");
          if ($left(a[1][4:2], d) !== 7 || $right(a[1][4:2], d) !== 4 || $size(a[1][4:2], d) !== 4)
            $fatal(1, "slice dim 2 mismatch");
        end
        3: begin
          if ($left(a, d) !== 7 || $right(a, d) !== 4 || $size(a, d) !== 4
              || $low(a, d) !== 4 || $high(a, d) !== 7 || $increment(a, d) !== 1)
            $fatal(1, "dim 3 mismatch");
        end
        default: begin
          if ($left(a, d) !== 'x || $size(a, d) !== 'x)
            $fatal(1, "out-of-range dim %0d not x", d);
        end
      endcase
    end
    if ($dimensions(a[1][4:2]) !== 2) $fatal(1, "slice dims");
    $display("PASSED");
    $finish;
  end
endmodule
