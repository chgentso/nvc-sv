// force and release of variables and nets
// Expected values: IEEE 1800-2017 10.6.2 (force/release): a force on a variable overrides procedural assignments
// until released, after release the variable keeps its value until the next assignment; a force on a net
// overrides its drivers and release returns the net to the driver value; the left-hand side of force may be a
// part-select of a net but not of a variable, so the part-select force is applied to a net vector.
`timescale 1ns/1ns
module svforce1_sub(input wire d, output wire y);
  assign y = d;
endmodule
module svforce1;
  reg clk = 0;
  reg [7:0] r = 8'h11;
  wire n;
  reg drv = 0;
  reg [7:0] nd = 8'h00;
  wire [7:0] nv = nd;
  int errs = 0;
  svforce1_sub s(.d(drv), .y(n));
  always #5 clk = ~clk;
  initial begin
    #1 force r = 8'hAA; if (r !== 8'hAA) begin $error("a r=%h", r); errs++; end
    r = 8'h55; #1 if (r !== 8'hAA) begin $error("b r=%h (assignment during force)", r); errs++; end
    release r; #1 if (r !== 8'hAA) begin $error("c r=%h (release keeps value)", r); errs++; end
    r = 8'h77; #1 if (r !== 8'h77) begin $error("d r=%h (assignment after release)", r); errs++; end
    if (n !== 1'b0) begin $error("n before force must be 0, got %b", n); errs++; end
    force n = 1'b1; #1 if (n !== 1'b1) begin $error("e n=%b", n); errs++; end
    drv = 1; #1 drv = 0; #1 if (n !== 1'b1) begin $error("f n=%b (driver change while forced)", n); errs++; end
    release n; #1 if (n !== 1'b0) begin $error("g n=%b (release returns driver value)", n); errs++; end
    if (nv !== 8'h00) begin $error("nv before force must be 0, got %h", nv); errs++; end
    force nv[3:0] = 4'hf; #1 if (nv !== 8'h0f) begin $error("h nv=%h", nv); errs++; end
    nd = 8'h30; #1 if (nv !== 8'h3f) begin $error("i nv=%h (other bits follow the driver)", nv); errs++; end
    release nv; #1 if (nv !== 8'h30) begin $error("j nv=%h (release returns driver value)", nv); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
