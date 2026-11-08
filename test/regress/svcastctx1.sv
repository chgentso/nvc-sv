// Expected values: IEEE 1800-2017 6.24.1 (casting): a size cast behaves as if the operand
// were assigned to a variable of the cast width (assignment-like context), so operands are
// extended to the cast width before the operation; signedness passes through a size cast;
// signed'/unsigned' use the operand's own width ($bits).  Sizing rules: 11.6, 11.8.
// 11.4.12.. shifts (11.4.10), 21.2.1 (%b, %0d).
module svcastctx1;
  typedef bit [7:0] byte_t;
  byte_t a = 8'd200, b = 8'd100;
  logic [7:0] la = 8'hf0, lb = 8'h20;
  logic signed [7:0] sa = -8'sd100, sb = -8'sd100;
  int i = 7;
  int errors = 0;
  longint g;
  task automatic chk(input longint got, input longint exp, input string what);
    if (got !== exp) begin $error("%s = %0d, expected %0d", what, got, exp); errors++; end
  endtask
  initial begin
    g = 9'(a + b);        chk(g, 300, "9'(a+b)");                    // 200+100 computed in 9 bits
    g = 9'(a + b) / 2;    chk(g, 150, "9'(a+b)/2");
    g = int'(a + b);      chk(g, 300, "int'(a+b)");
    g = int'(a) + int'(b);chk(g, 300, "int'(a)+int'(b)");
    g = 16'(a * b);       chk(g, 20000, "16'(a*b)");
    g = 16'(a - b - b - b); chk(g, 65436, "16'(a-b-b-b)");           // -100 mod 2**16
    g = 8'(a + b);        chk(g, 44, "8'(a+b)");                     // 300 mod 256
    g = 4'(a + b);        chk(g, 12, "4'(a+b)");                     // 300 mod 16
    if (9'(la + lb) !== 9'b100010000) begin $error("9'(la+lb) = %b", 9'(la + lb)); errors++; end
    if (12'(~la) !== 12'b111100001111) begin $error("12'(~la) = %b", 12'(~la)); errors++; end
    g = 9'(sa + sb);      chk(g, -200, "9'(sa+sb)");                 // signed, extended: -200 fits in 9 bits
    g = 16'(sa + sb);     chk(g, -200, "16'(sa+sb)");
    g = int'(sa * sb);    chk(g, 10000, "int'(sa*sb)");
    g = longint'(a) * 100000000; chk(g, 64'd20000000000, "longint'(a)*100000000");
    g = longint'(a * 10000000);  chk(g, 64'd2000000000, "longint'(a*10000000)");
    g = byte_t'(a + b);   chk(g, 44, "byte_t'(a+b)");
    g = shortint'(a + b); chk(g, 300, "shortint'(a+b)");
    g = int'(a > b ? a + b : b + b); chk(g, 300, "int'(a>b ? a+b : b+b)");
    g = 10'(a + b) >> 1;  chk(g, 150, "10'(a+b)>>1");
    g = int'(-a);         chk(g, -200, "int'(-a)");
    if (12'(-la) !== 12'b111100010000) begin $error("12'(-la) = %b", 12'(-la)); errors++; end
    g = unsigned'(sa + sb); chk(g, 56, "unsigned'(sa+sb)");          // 8-bit sum -200 mod 256, unsigned
    g = signed'(a + b);   chk(g, 44, "signed'(a+b)");                // 8-bit sum 44, signed
    g = 33'(i - 8);       chk(g, -1, "33'(i-8)");                    // int is signed: stays -1
    g = 40'(a) << 32;     chk(g, 64'd858993459200, "40'(a) << 32");      // 200 * 2**32
    g = 40'(a << 32);     chk(g, 64'd858993459200, "40'(a << 32)");      // operand extended to 40 bits first
    if (errors == 0) $display("PASSED");
  end
endmodule
