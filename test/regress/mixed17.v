// Verilog top-level instantiating a VHDL entity with vector generics
// which have aggregate and function call default values
module mixed17;
    wire o1, o2;

    mixed_def u1 (.o(o1));
    mixed_def #(.A(8'h12), .F(3'b010)) u2 (.o(o2));

    initial begin
        #1;
        if (o1 !== 1'b1 || o2 !== 1'b1)
            $display("FAILED: %b %b", o1, o2);
        else
            $display("PASSED");
        $finish;
    end
endmodule
