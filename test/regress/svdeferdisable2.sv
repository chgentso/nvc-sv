// A deferred immediate assertion disabled by its label.
// IEEE 1800-2017 16.4.4: a specific deferred assertion may be disabled with a
// disable statement naming its label (the label names the assertion, 16.3
// and 9.3.5); the pending reports of that assertion are cancelled and those
// of other assertions of the process are kept.
`timescale 1ns/1ns
module svdeferdisable2;
  string reps[$];
  function void rep(string id, int v);
    reps.push_back($sformatf("%0t:%s:%0d", $time, id, v));
  endfunction

  int tc = 0;
  always @(tc) begin
    a4: assert #0 (tc == 0) else rep("a4", tc);
    a5: assert #0 (tc == 0) else rep("a5", tc);
    if (tc == 1) disable a4;
  end

  always @(tc) begin : nb
    a6: assert #0 (tc == 0) else rep("a6", tc);
    if (tc == 2) disable a6;
    a7: assert final (tc == 0) else rep("a7", tc);
    if (tc == 1) disable a7;
  end

  initial begin
    #10 tc = 1;                 // a5 and a6 reported
    #10 tc = 2;                 // a4, a5 and a7 reported
  end

  initial begin
    automatic string exp[$] = '{"10:a5:1", "10:a6:1", "20:a4:2", "20:a5:2",
                                "20:a7:2"};
    #100;
    reps.sort();
    exp.sort();
    if (reps != exp) begin
      $display("reports %p", reps);
      $display("expected %p", exp);
      $error("mismatch");
    end
    else
      $display("PASSED");
  end
endmodule
