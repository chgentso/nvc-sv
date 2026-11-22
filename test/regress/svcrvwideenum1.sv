package svcrvwideenum1_pkg;
  typedef enum logic [127:0] {
    StA = 128'h1111_2222_3333_4444_5555_6666_7777_8888,
    StB = 128'haaaa_bbbb_cccc_dddd_eeee_ffff_0123_4567,
    StC = 128'hdead_beef_cafe_babe_0011_2233_4455_6677
  } wide_state_e;

  class item_c;
    rand wide_state_e st;
    constraint not_b_c { st != StB; }
  endclass
endpackage

module svcrvwideenum1;
  import svcrvwideenum1_pkg::*;

  initial begin
    automatic wide_state_e s;
    for (int i = 0; i < 20; i++) begin
      if (!std::randomize(s))
        $fatal(1, "std::randomize failed");
      if (s !== StA && s !== StB && s !== StC)
        $fatal(1, "invalid std::randomize value %h", s);
    end

    for (int i = 0; i < 20; i++) begin
      if (!std::randomize(s) with { s != StA; })
        $fatal(1, "std::randomize with failed");
      if (s !== StB && s !== StC)
        $fatal(1, "constraint violated %h", s);
    end

    begin
      automatic item_c it = new();
      for (int i = 0; i < 20; i++) begin
        if (!it.randomize())
          $fatal(1, "class randomize failed");
        if (it.st !== StA && it.st !== StC)
          $fatal(1, "invalid class value %h", it.st);
      end
    end

    $display("PASSED");
  end
endmodule
