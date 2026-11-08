class a;
  rand int B[5];
  rand bit [7:0] C[3];
  int fixed_arr[3] = '{1, 2, 3};
  constraint c {
    B.sum() == 5;
    foreach (B[i]) B[i] >= 0;
    C.sum() == 8'd10;
    C.or() == 8'd7;
  }
endclass

module svcrvreduce1;
  a obj = new;
  int bad = 0;
  initial begin
    repeat (20) begin
      if (!obj.randomize()) $fatal(1, "randomize failed");
      if (obj.B.sum() != 5) bad++;
      if (obj.C.sum() != 10 || obj.C.or() != 7) bad++;
    end
    if (bad != 0) $fatal(1, "%0d bad solutions", bad);
    $display("PASSED");
  end
endmodule
