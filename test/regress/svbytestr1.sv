module svbytestr1;
  byte desc [3:0] = "AB\n";
  byte asc [0:3];
  byte unsized [4];
  byte padded [3:0] = "XY";
  byte trunc [1:0] = "WXYZ";
  byte nested [0:1][3:0] = '{"AB\n", "CD\t"};

  assign asc = "AB\n";

  initial begin
    #1;

    if (desc[3] !== 8'h41 || desc[2] !== 8'h42 || desc[1] !== 8'h0a || desc[0] !== 8'h00)
      $fatal(1, "desc mismatch: %h %h %h %h", desc[3], desc[2], desc[1], desc[0]);

    if (asc[0] !== 8'h41 || asc[1] !== 8'h42 || asc[2] !== 8'h0a || asc[3] !== 8'h00)
      $fatal(1, "asc mismatch: %h %h %h %h", asc[0], asc[1], asc[2], asc[3]);

    unsized = "AB";
    if (unsized[0] !== 8'h41 || unsized[1] !== 8'h42 || unsized[2] !== 8'h00 || unsized[3] !== 8'h00)
      $fatal(1, "unsized pad mismatch");

    unsized = "ABCDE";
    if (unsized[0] !== 8'h41 || unsized[1] !== 8'h42 || unsized[2] !== 8'h43 || unsized[3] !== 8'h44)
      $fatal(1, "unsized trunc mismatch");

    if (padded[3] !== 8'h58 || padded[2] !== 8'h59 || padded[1] !== 8'h00 || padded[0] !== 8'h00)
      $fatal(1, "padded mismatch");

    if (trunc[1] !== 8'h57 || trunc[0] !== 8'h58)
      $fatal(1, "trunc mismatch");

    if (nested[0][3] !== 8'h41 || nested[0][2] !== 8'h42 || nested[0][1] !== 8'h0a || nested[0][0] !== 8'h00)
      $fatal(1, "nested[0] mismatch");
    if (nested[1][3] !== 8'h43 || nested[1][2] !== 8'h44 || nested[1][1] !== 8'h09 || nested[1][0] !== 8'h00)
      $fatal(1, "nested[1] mismatch");

    begin
      automatic byte loc [0:3] = "Hi";
      if (loc[0] !== 8'h48 || loc[1] !== 8'h69 || loc[2] !== 8'h00 || loc[3] !== 8'h00)
        $fatal(1, "loc mismatch");
    end

    $display("PASSED");
  end
endmodule
