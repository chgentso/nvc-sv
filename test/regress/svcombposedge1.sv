// Pins current behaviour, not an LRM requirement: a variable which an
// always_comb process raises and lowers again within one activation (0 -> 1 ->
// 0) is an update event for explicit waiters (IEEE 1800-2017 9.4.2) but an
// edge control is evaluated against the final value, so @(posedge p) does not
// resume.  A change that makes it resume is a deliberate one and should
// update this test.
module svcombposedge1;
  logic d = 0, p;
  int nany = 0, npos = 0;
  always_comb begin p = 1'b0; if (d) begin p = 1'b1; p = 1'b0; end end
  always @(p) nany++;
  always @(posedge p) npos++;
  initial begin
    #1 d = 1;
    #1 if (npos != 0) $error("posedge seen %0d times", npos);
    if (nany != 1) $error("any-change seen %0d times, expected 1", nany);
    $display("PASSED");
  end
endmodule
