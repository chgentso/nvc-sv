module svcovcrosswith1;
  bit a, b;

  covergroup cg;
    a_cp: coverpoint a;
    b_cp: coverpoint b;
    ab_cross: cross a_cp, b_cp {
      ignore_bins skip_10 = ab_cross with (a_cp == 1 && b_cp == 0);
    }
  endgroup

  initial begin
    automatic cg c = new();
    // Only 3 bins should remain: (0,0), (0,1), (1,1).
    a = 0; b = 0; c.sample();
    a = 0; b = 1; c.sample();
    a = 1; b = 1; c.sample();
    if (c.ab_cross.get_inst_coverage() < 99.9)
      $fatal(1, "expected 100%% cross coverage, got %f", c.ab_cross.get_inst_coverage());
    $display("PASSED");
    $finish;
  end
endmodule
