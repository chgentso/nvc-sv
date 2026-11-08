// Postponed processes reading VHDL ports driven by Verilog
`timescale 1ns/1ns
module mixed29;
    reg clk;
    reg [1:0] v;
    wire [31:0] rise, ev, slice;

    mixed29_e dut (clk, v, rise, ev, slice);

    initial begin
        clk = 0;
        v = 0;
        #5 clk = 1;
        v = 2;
        #5 clk = 0;
        v = 0;
        #5 clk = 1;
        v = 2;
        #5;
        if (rise !== 2 || ev !== 4 || slice !== 4)
            $display("FAILED: %0d %0d %0d", rise, ev, slice);
        else
            $display("PASSED");
        $finish;
    end
endmodule
