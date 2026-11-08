// Verilog top-level instantiating a VHDL entity with vector generics
module mixed16;
    wire [7:0] o1, o2, o3;

    mixed_vec u1 (.o(o1));
    mixed_vec #(.MASK(8'h3C), .INIT(4'd9), .BV(5'b10101), .PAT(4'bzx01))
        u2 (.o(o2));
    mixed_vec #(8'hFF, 4'd1, 2'b01) u3 (.o(o3));

    initial begin
        #1;
        if (o1 !== 8'hA5 || o2 !== 8'h3C || o3 !== 8'hFF)
            $display("FAILED: %h %h %h", o1, o2, o3);
        else
            $display("PASSED");
        $finish;
    end
endmodule
