// The names of an enumeration declared in the target module are visible
// in the connections of a bind directive (IEEE 1800-2017 23.11: the
// instantiation is as if written in the target module).
module svbindenum1_cmp #(parameter int NW = 8) ();
  localparam int AW = $clog2(NW);
  typedef enum logic [4:0] { Waiting = 5'b00100, Done = 5'b11001 } state_e;
endmodule

interface svbindenum1_probe #(parameter int StateWidth = 1,
                              parameter logic [4:0] Waiting = 0,
                              parameter logic [4:0] Done = 0,
                              parameter int AW = 0);
  initial begin
    #1;
    if (Waiting !== 5'd4 || Done !== 5'd25 || StateWidth != 5 || AW != 3)
      $fatal(1, "bad parameters %0d %0d %0d %0d", Waiting, Done, StateWidth, AW);
    $display("PASSED");
  end
endinterface

module svbindenum1;
  svbindenum1_cmp c();
  bind svbindenum1_cmp svbindenum1_probe #(.StateWidth($bits(Waiting)),
      .Waiting(Waiting), .Done(Done), .AW(AW)) u_p();
  initial #2 $finish;
endmodule
