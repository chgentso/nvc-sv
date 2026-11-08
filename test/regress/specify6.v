// $setup / $hold timing checks with fractional limits and notifiers.
// Stimulus: d changes at 4, 5.5 and 6.75 ns, clk rises at 5 and 15 ns.
// Expected values, IEEE 1800-2017 31.3.1 / 31.3.3 ($setup, $hold) and 31.5
// (notifiers: a reg toggled when the check is violated):
//  - $setup(d, posedge clk, L) is violated when 0 <= Tref - Tdata < L:
//    d at 4, edge at 5 gives 1 ns, so limits 2 and 1.1 are violated at 5 ns,
//    limit 0.9 is not;
//  - $hold(posedge clk, d, 1.5) is violated when 0 <= Tdata - Tref < 1.5:
//    d at 5.5 is 0.5 ns after the edge (violation, reported at 5.5), d at
//    6.75 is 1.75 ns after it (no violation).
// The text of any violation message is implementation-defined and not
// checked; the notifier toggles are.
`timescale 1ns/1ps

module specify6;

    reg d   = 1'b0;
    reg clk = 1'b0;
    integer errors = 0;


    sub i_sub(
        .d   (d),
        .clk (clk)
    );

    always #5 clk = ~clk;

    initial
    begin
        #4;
        d = 1'b1;
        #1.5;
        d = 1'b0;
        #1.25;
        d = 1'b1;
        #10;
        // 16.75: the notifier toggles recorded by the sub-module
        if (i_sub.cnt2 !== 1 || i_sub.t2 != 5000) begin
            $error("setup 2: %0d toggles, last at %0d ps (expected 1 at 5000)", i_sub.cnt2, i_sub.t2);
            errors = errors + 1;
        end
        if (i_sub.cnt11 !== 1 || i_sub.t11 != 5000) begin
            $error("setup 1.1: %0d toggles, last at %0d ps (expected 1 at 5000)", i_sub.cnt11, i_sub.t11);
            errors = errors + 1;
        end
        if (i_sub.cnt09 !== 0) begin
            $error("setup 0.9: %0d toggles (expected 0)", i_sub.cnt09);
            errors = errors + 1;
        end
        if (i_sub.cnth !== 1 || i_sub.th != 5500) begin
            $error("hold 1.5: %0d toggles, last at %0d ps (expected 1 at 5500)", i_sub.cnth, i_sub.th);
            errors = errors + 1;
        end
        if (errors == 0) $display("PASSED");
        $finish;
    end

endmodule

module sub(
    input d,
    input clk
);

  reg n2 = 1'b0, n11 = 1'b0, n09 = 1'b0, nh = 1'b0;
  integer cnt2 = 0, cnt11 = 0, cnt09 = 0, cnth = 0;
  integer t2 = -1, t11 = -1, th = -1;      // time of the last toggle in ps

  always @(n2)  begin cnt2 = cnt2 + 1;   t2 = $realtime * 1000; end
  always @(n11) begin cnt11 = cnt11 + 1; t11 = $realtime * 1000; end
  always @(n09) cnt09 = cnt09 + 1;
  always @(nh)  begin cnth = cnth + 1;   th = $realtime * 1000; end

  specify
    // The limits are in units of the time scale
    $setup(d, posedge clk, 2, n2);
    $setup(d, posedge clk, 1.1, n11);
    $setup(d, posedge clk, 0.9, n09);
    $hold(posedge clk, d, 1.5, nh);
  endspecify

endmodule
