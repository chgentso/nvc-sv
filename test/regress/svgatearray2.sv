// Expected values: IEEE 1800-2017 28.3.6 (arrays of primitive instances: instance k takes bit k of
// vector terminals), 28.4 (bufif1: z when enable is 0), 28.5 (buf), 11.4.8 (bitwise operators).
// bufif1 b[3:0]: enable = 0110, data 1010 -> pad = z01z.  buf c[3:0]: input ~1010 ^ 0101 = 0000.
module svgatearray2;
  wire [3:0] pad;
  reg [3:0] d = 4'b1010, en = 4'b0110;
  reg oe = 1;
  bufif1 b [3:0] (pad, d, en & {4{oe}});
  wire [3:0] n;
  buf c [3:0] (n, ~d ^ {2{2'b01}});
  int bad = 0;
  initial begin
    #1;
    if (pad !== 4'bz01z) begin $error("pad=%b, want z01z", pad); bad++; end
    if (n !== 4'b0000) begin $error("n=%b, want 0000", n); bad++; end
    oe = 0;
    #1;
    if (pad !== 4'bzzzz) begin $error("pad=%b, want zzzz", pad); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
