// Verilog clock driving an edge-triggered VHDL process
module mixed13;
    reg        clk;
    reg  [3:0] d;
    wire [3:0] q;

    reg4 dut (clk, d, q);

    initial begin
        clk = 0;
        d = 4'h5;
        #5 clk = 1;
        #5;
        if (q !== 4'h5)
            $display("FAILED: q=%h", q);
        d = 4'ha;
        #5 clk = 0;
        #5 clk = 1;
        #5;
        if (q !== 4'ha)
            $display("FAILED: q=%h", q);
        else
            $display("PASSED");
        $finish;
    end
endmodule
