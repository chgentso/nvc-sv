// Verilog top-level instantiating a VHDL entity with generics
module mixed11;
    reg  [7:0] a, b;
    reg  [3:0] a4, b4;
    wire [7:0] y8, y8b;
    wire [3:0] y4;

    genadd #(.W(8), .K(1)) u1 (.a(a), .b(b), .y(y8));
    genadd #(.k(2), .inv(1)) u2 (.a(a4), .b(b4), .y(y4));
    genadd #(8, 1, 3) u3 (.a(a), .b(b), .y(y8b));

    initial begin
        a = 10; b = 20; a4 = 9; b4 = 4;
        #1;
        if (y8 !== 31 || y4 !== 7 || y8b !== 249)
            $display("FAILED: %0d %0d %0d", y8, y4, y8b);
        else
            $display("PASSED");
        $finish;
    end
endmodule
