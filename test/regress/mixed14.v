// Verilog top-level instantiating a VHDL entity with ports of
// UNSIGNED, SIGNED, BIT, BIT_VECTOR, and INTEGER types
module mixed14;
    reg  [3:0]         u, s, bv;
    reg                b;
    reg  [31:0]        n;
    wire [3:0]         ou, os, obv;
    wire               ob;
    wire signed [31:0] oi;
    wire [15:0]        wide;
    wire [1:0]         mm;

    mixed_types #(4) dut (.u(u), .s(s), .bv(bv), .b(b), .n(n), .ou(ou),
                          .os(os), .obv(obv), .ob(ob), .oi(oi),
                          .wide(wide), .mm(mm));

    initial begin
        u = 4'd5; s = 4'sd3; bv = 4'b1010; b = 0; n = 7;
        #1;
        if (ou !== 6 || os !== 2 || obv !== 4'b0101 || ob !== 1
            || oi !== -3 || wide !== 16'hffff || mm !== 2'b11)
            $display("FAILED: %d %d %b %b %0d %h %b", ou, os, obv, ob, oi,
                     wide, mm);
        else
            $display("PASSED");
        $finish;
    end
endmodule
