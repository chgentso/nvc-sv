// Events on VHDL ports driven by Verilog
`timescale 1ns/1ns
module mixed26;
    reg clk;
    reg [1:0] v;
    wire [31:0] rise, fall, ev, slice;

    mixed26_e dut (clk, v, rise, fall, ev, slice);

    initial begin
        clk = 0;
        v = 0;
        // A pulse in consecutive delta cycles
        #5 clk = 1;
        v = 2;
        #0 clk = 0;
        v = 0;
        #0 clk = 1;
        v = 2;
        #5 clk = 0;
        v = 0;
        #5 clk = 1;
        v = 2;
        #5;
        if (rise !== 3 || fall !== 2 || ev !== 6 || slice !== 6)
            $display("FAILED: %0d %0d %0d %0d", rise, fall, ev, slice);
        else
            $display("PASSED");
        $finish;
    end
endmodule
