`timescale 1ns/1ns
// Nonblocking assignments to output/inout formals of a static task, copied out when the task returns
// Expected values: IEEE 1800-2017 13.5 (output and inout arguments are copied out when the call returns; a
// nonblocking assignment to a formal that is still pending then updates the formal only, later), 10.4.2, 11.5.1
package pk;
  task static pt(inout int a, output int b);
    a <= a + 1;
    b <= a;
    #2;
    a <= a + 100;
  endtask
endpackage
module svnbacopy1;
  task mt(inout int a, output int b);
    a <= a + 1;
    b <= a;
    #2;
    a <= a + 100;
  endtask
  int x = 5, y = 7, x2 = 5, y2 = 7, x3 = 5, y3 = 7;
  logic [71:0] w = 72'd9, w2;
  int fails = 0;
  task wt(inout logic [71:0] a, output logic [71:0] b);
    a[3:0] <= 4'd1;
    b <= a;
    #2;
  endtask
  initial begin
    // The task body: a <= a+1 and b <= a are applied at t=0, a <= a+100 is still pending when the task returns
    // at t=2, so the copy-out sees a=6 (5+1) and b=5.
    mt(x, y);
    if ($time != 2 || x !== 6 || y !== 5) begin $error("mt: t=%0t x=%0d y=%0d", $time, x, y); fails++; end
    #1;
    if ($time != 3 || x !== 6 || y !== 5) begin $error("mt later: t=%0t x=%0d y=%0d", $time, x, y); fails++; end
    pk::pt(x2, y2);
    if ($time != 5 || x2 !== 6 || y2 !== 5) begin $error("pt: t=%0t x=%0d y=%0d", $time, x2, y2); fails++; end
    #1;
    if ($time != 6 || x2 !== 6 || y2 !== 5) begin $error("pt later: t=%0t x=%0d y=%0d", $time, x2, y2); fails++; end
    // here the nonblocking assignment to the formal is applied while the task still waits, so it is copied out
    wt(w, w2);
    if ($time != 8 || w !== 72'd1 || w2 !== 72'd9) begin $error("wt: t=%0t w=%0d w2=%0d", $time, w, w2); fails++; end
    #1;
    if ($time != 9 || w !== 72'd1 || w2 !== 72'd9) begin $error("wt later: t=%0t w=%0d w2=%0d", $time, w, w2); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
