// Expected values: IEEE 1800-2017 12.4 (a condition is true if it is non-zero and not x/z: some bit 1),
// 11.4.7 (logical operators), 11.4.9 (reduction), 11.4.5 (equality), 11.4.6 (bitwise), 11.4.11 (conditional with
// unknown condition merges bitwise), 12.5 (case uses ===), 12.6 / 16.3 (immediate assert, x condition fails)
module svtruth1;
  logic [3:0] a = 4'b0x1, b = 4'b0x0, c = 4'bz000, d = 4'b1xxx;
  int n;
  int errs;
  int fa, fb;
  logic [1:0] t2;
  logic [31:0] w32;
  logic r1, r2, r3;
  initial begin
    if (a) begin end else begin $error("a must be true"); errs++; end
    if (b) begin $error("b must not be true"); errs++; end
    if (c) begin $error("c must not be true"); errs++; end
    if (d) begin end else begin $error("d must be true"); errs++; end
    if (!a) begin $error("!a must be false"); errs++; end
    if (!b) begin $error("!b is x, not true"); errs++; end
    r1 = !a; r2 = !b; r3 = !c;
    if (r1 !== 1'b0 || r2 !== 1'bx || r3 !== 1'bx || (!d) !== 1'b0) begin $error("! %b %b %b", r1, r2, r3); errs++; end
    if ((a && 1) !== 1'b1 || (a || 0) !== 1'b1) begin $error("a&&1, a||0"); errs++; end
    if ((b && 1) !== 1'bx || (b || 0) !== 1'bx) begin $error("b&&1, b||0"); errs++; end
    if ((b && 0) !== 1'b0 || (b || 1) !== 1'b1) begin $error("b&&0, b||1"); errs++; end
    t2 = a ? 2'b11 : 2'b00; if (t2 !== 2'b11) begin $error("a?"); errs++; end
    t2 = b ? 2'b11 : 2'b00; if (t2 !== 2'bxx) begin $error("b?"); errs++; end
    t2 = c ? 2'b11 : 2'b00; if (t2 !== 2'bxx) begin $error("c?"); errs++; end
    t2 = d ? 2'b11 : 2'b00; if (t2 !== 2'b11) begin $error("d?"); errs++; end
    n = 0; while (a) begin n++; if (n > 2) break; end
    if (n != 3) begin $error("while a n=%0d", n); errs++; end
    n = 0; while (b) begin n++; if (n > 2) break; end
    if (n != 0) begin $error("while b n=%0d", n); errs++; end
    w32 = a & 1; if (w32 !== 32'd1) begin $error("a&1 %b", w32); errs++; end
    w32 = a | 0; if (w32 !== {28'b0, 4'b0x1}) begin $error("a|0 %b", w32); errs++; end
    if ((&a) !== 1'b0 || (|a) !== 1'b1 || (^a) !== 1'bx) begin $error("reductions of a"); errs++; end
    if ((|b) !== 1'bx || (|c) !== 1'bx) begin $error("|b |c"); errs++; end
    case (1)
      a: begin $error("case must not match a"); errs++; end
      default: ;
    endcase
    r1 = 0; if (a ? 1'b1 : 1'b0) r1 = 1;
    if (r1 !== 1'b1) begin $error("tern true"); errs++; end
    if ((a == 4'b0x1) !== 1'bx) begin $error("a == literal"); errs++; end
    if ((a != 4'b0x1) !== 1'bx) begin $error("a != literal"); errs++; end
    assert (a) else fa++;
    assert (b) else fb++;
    if (fa != 0) begin $error("assert a failed"); errs++; end
    if (fb != 1) begin $error("assert b must fail (x condition), fb=%0d", fb); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
