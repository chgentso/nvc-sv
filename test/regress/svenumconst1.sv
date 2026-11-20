module svenumconst1;
  typedef enum logic [3:0] {
    E1 = 4'd1,
    E3 = 4'd3,
    E7 = 4'd7
  } my_e;

  my_e e;
  int arr[e.num];
  localparam int N = e.num;
  localparam my_e F = e.first;
  localparam my_e L = e.last;

  initial begin
    if ($size(arr) !== 3) $fatal(1, "size=%0d", $size(arr));
    if (N !== 3) $fatal(1, "N=%0d", N);
    if (F !== E1) $fatal(1, "F=%0d", F);
    if (L !== E7) $fatal(1, "L=%0d", L);
    $display("PASSED");
    $finish;
  end
endmodule
