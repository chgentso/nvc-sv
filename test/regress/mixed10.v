// Verilog top-level instantiating a VHDL entity
module mixed10;
    reg  [3:0] a, b;
    reg        c;
    wire [4:0] sum;
    wire       z;
    integer    i, j, errors;

    adder dut (.a(a), .b(b), .c(c), .sum(sum), .z(z));

    initial begin
        errors = 0;
        for (i = 0; i < 16; i = i + 1)
            for (j = 0; j < 16; j = j + 1) begin
                a = i; b = j; c = i[0];
                #1;
                if (sum !== i + j + c) begin
                    $display("FAILED: %0d + %0d + %0d = %0d", i, j, c, sum);
                    errors = errors + 1;
                end
                if (z !== (i + j + c == 0)) begin
                    $display("FAILED: z=%b for sum %0d", z, sum);
                    errors = errors + 1;
                end
            end
        if (errors == 0)
            $display("PASSED");
        $finish;
    end
endmodule
