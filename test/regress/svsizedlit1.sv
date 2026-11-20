`define W 8
`define WIDE 128
`define MAX_VAL `W'd255

module svsizedlit1;
  logic [`W-1:0] a;
  logic [`WIDE-1:0] b;
  logic [15:0] c;

  initial begin
    a = `MAX_VAL;
    if (a !== 8'd255) $fatal(1, "a=%h", a);
    if ($bits(`MAX_VAL) !== 8) $fatal(1, "bits(MAX_VAL)=%0d", $bits(`MAX_VAL));

    b = `WIDE'hda7ada7a;
    if (b !== 128'hda7ada7a) $fatal(1, "b=%h", b);
    if ($bits(`WIDE'hda7ada7a) !== 128) $fatal(1, "bits(b)=%0d", $bits(`WIDE'hda7ada7a));

    c = 16 'h1234;
    if (c !== 16'h1234) $fatal(1, "c=%h", c);

    $display("PASSED");
    $finish;
  end
endmodule
