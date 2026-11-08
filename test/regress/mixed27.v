// Events on VHDL inout ports driven by Verilog
`timescale 1ns/1ns
module mixed27;
    reg v;
    wire a, b, c;
    wire [31:0] a_rise, b_rise;
    reg c_en;

    assign a = v;
    assign b = v;
    assign c = c_en ? v : 1'bz;

    mixed27_e dut (a, b, c, a_rise, b_rise);

    initial begin
        v = 0;
        c_en = 1;
        #5 v = 1;
        #5 v = 0;
        #5 v = 1;
        #1;
        if (a !== 1 || b !== 1)
            $display("FAILED: a=%b b=%b", a, b);
        if (a_rise !== 2 || b_rise !== 2)
            $display("FAILED: a_rise=%0d b_rise=%0d", a_rise, b_rise);
        // The entity drives the net when Verilog releases it
        c_en = 0;
        #1;
        if (c !== 1)
            $display("FAILED: c=%b", c);
        else
            $display("PASSED");
        $finish;
    end
endmodule
