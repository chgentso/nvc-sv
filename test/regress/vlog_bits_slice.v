module vlog_bits_slice;
  byte arr[7:0][2:0];
  logic [15:0] packed_v;

  initial begin
    int i;
    i = 4;
    if ($bits(arr) !== 192) $fatal(1, "arr=%0d", $bits(arr));
    if ($bits(arr[0]) !== 24) $fatal(1, "arr[0]=%0d", $bits(arr[0]));
    if ($bits(arr[1:0]) !== 48) $fatal(1, "arr[1:0]=%0d", $bits(arr[1:0]));
    if ($bits(arr[1+:3]) !== 72) $fatal(1, "arr[1+:3]=%0d", $bits(arr[1+:3]));
    if ($bits(arr[4-:4]) !== 96) $fatal(1, "arr[4-:4]=%0d", $bits(arr[4-:4]));
    if ($bits(arr[i-:2]) !== 48) $fatal(1, "arr[i-:2]=%0d", $bits(arr[i-:2]));
    if ($bits(arr[i+:2]) !== 48) $fatal(1, "arr[i+:2]=%0d", $bits(arr[i+:2]));
    if ($bits(packed_v[7:0]) !== 8) $fatal(1, "packed_v[7:0]=%0d", $bits(packed_v[7:0]));
    $display("PASS");
  end
endmodule
