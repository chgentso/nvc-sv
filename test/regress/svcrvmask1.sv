// Self-checking: a rand variable ANDed with a constant mask equals a constant, with the mask in the
// high, middle or scattered bits.  The unmasked bits stay free.
// Expected values: IEEE 1800-2017 18.5 (constraints), 11.4.8 (bitwise AND).
class M;
  rand bit [31:0] a;
  rand bit [63:0] b;
  rand bit [47:0] c;
  constraint ca { (a & 32'hffff0000) == 32'h12340000; }
  constraint cb { (b & 64'hff00_0000_0000_00ff) == 64'h1200_0000_0000_0034; }
  constraint cc { (c & 48'h0f0f_0f0f_0f0f) == 48'h0102_0304_0506; }
endclass
module svcrvmask1;
  M m = new;
  int bad;
  bit [31:0] a_or, a_and;
  bit [63:0] b_or, b_and;
  initial begin
    a_and = '1; b_and = '1;
    for (int i = 0; i < 100; i++) begin
      if (!m.randomize()) begin $error("randomize failed"); bad++; end
      else begin
        if ((m.a & 32'hffff0000) !== 32'h12340000) begin $error("a %h", m.a); bad++; end
        if ((m.b & 64'hff00_0000_0000_00ff) !== 64'h1200_0000_0000_0034) begin $error("b %h", m.b); bad++; end
        if ((m.c & 48'h0f0f_0f0f_0f0f) !== 48'h0102_0304_0506) begin $error("c %h", m.c); bad++; end
        a_or |= m.a; a_and &= m.a; b_or |= m.b; b_and &= m.b;
      end
    end
    if ((a_or | 32'hffff0000) != '1 || (a_and & 32'h0000ffff) != 0) begin $error("a bits not free"); bad++; end
    if ((b_or | 64'hff00_0000_0000_00ff) != '1 || (b_and & 64'h00ff_ffff_ffff_ff00) != 0) begin
      $error("b bits not free"); bad++;
    end
    if (bad == 0) $display("PASSED");
  end
endmodule
