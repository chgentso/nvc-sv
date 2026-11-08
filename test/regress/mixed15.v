// Verilog top-level instantiating a VHDL entity with real, string,
// and boolean generics
module mixed15;
    wire [2:0] o1;
    wire [4:0] o2;
    wire [2:0] o3;

    mixed_gen u1 (.o(o1));
    mixed_gen #(.NAME("hello \"x\""), .GAIN(2.5), .W(5), .FLAG(1)) u2 (.o(o2));
    mixed_gen #("abc", 3) u3 (.o(o3));

    initial begin
        #1;
        if (o1 !== 3'b111 || o2 !== 5'b11111 || o3 !== 3'b111)
            $display("FAILED: %b %b %b", o1, o2, o3);
        else
            $display("PASSED");
        $finish;
    end
endmodule
