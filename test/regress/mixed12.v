// Verilog top-level instantiating a VHDL entity with inout ports
module mixed12;
    reg        oe, drv_en, s_drv;
    reg  [7:0] din, drv;
    wire [7:0] d, q;
    wire       s;

    assign d = drv_en ? drv : 8'bz;
    assign s = drv_en ? s_drv : 1'bz;

    inout_if u (.oe(oe), .d_in(din), .d(d), .q(q), .s(s));

    initial begin
        oe = 1; din = 8'h5a; drv_en = 0; s_drv = 0;
        #1;
        if (d !== 8'h5a || q !== 8'h5a || s !== 1'b1)
            $display("FAILED: VHDL drive d=%h q=%h s=%b", d, q, s);

        oe = 0; drv_en = 1; drv = 8'ha5;
        #1;
        if (d !== 8'ha5 || q !== 8'ha5 || s !== 1'b0)
            $display("FAILED: Verilog drive d=%h q=%h s=%b", d, q, s);
        else
            $display("PASSED");
        $finish;
    end
endmodule
