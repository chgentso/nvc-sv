// Expected values: IEEE 1800-2017 12.4 (truth of wide vectors with x/z), 11.4.7 (logical), 11.4.9 (reduction
// operators on 4-state values), 11.4.11 (conditional with unknown condition)
module svtruth2;
  logic [99:0] w1 = {99'bx, 1'b1}, w2 = {99'bx, 1'b0}, w3 = {100{1'b0}}, w4 = {1'b1, 99'bx};
  logic [99:0] w5 = {100{1'b1}}, w6 = {1'b0, {99{1'b1}}}, w7 = {1'bx, {99{1'b1}}}, w8 = {100{1'bz}};
  int errs;
  logic [7:0] ro, ra, rx;
  logic [3:0] rn;
  logic [31:0] t;
  initial begin
    if (w1) begin end else begin $error("w1 must be true"); errs++; end
    if (w2) begin $error("w2 must not be true"); errs++; end
    if (w3) begin $error("w3 must not be true"); errs++; end
    if (w4) begin end else begin $error("w4 must be true"); errs++; end
    if ((!w1) !== 1'b0 || (!w2) !== 1'bx || (!w3) !== 1'b1 || (!w4) !== 1'b0) begin $error("logical not"); errs++; end
    if ((w1 && 1) !== 1'b1 || (w2 && 1) !== 1'bx) begin $error("&& 1"); errs++; end
    t = w1 ? 1 : 0; if (t !== 32'd1) begin $error("w1 ?: %b", t); errs++; end
    t = w2 ? 1 : 0; if (t !== {31'b0, 1'bx}) begin $error("w2 ?: %b", t); errs++; end
    ro = {|w1, |w2, |w3, |w4, |w5, |w6, |w7, |w8};
    if (ro !== {1'b1, 1'bx, 1'b0, 1'b1, 1'b1, 1'b1, 1'b1, 1'bx}) begin $error("| reductions %b", ro); errs++; end
    ra = {&w1, &w2, &w3, &w4, &w5, &w6, &w7, &w8};
    if (ra !== {1'bx, 1'b0, 1'b0, 1'bx, 1'b1, 1'b0, 1'bx, 1'bx}) begin $error("& reductions %b", ra); errs++; end
    rx = {^w1, ^w2, ^w3, ^w4, ^w5, ^w6, ^w7, ^w8};
    if (rx !== {1'bx, 1'bx, 1'b0, 1'bx, 1'b0, 1'b1, 1'bx, 1'bx}) begin $error("^ reductions %b", rx); errs++; end
    rn = {~|w1, ~|w2, ~&w1, ~&w7};
    if (rn !== {1'b0, 1'bx, 1'bx, 1'bx}) begin $error("nor/nand %b", rn); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
