// IEEE 1800-2017 27.4: the loop variable of a generate for is a genvar.
module genvar3;
  integer i;
  genvar g;
  generate
    for (i = 0; i < 3; i = i + 1) begin         // Error
    end
    for (g = 0; g < 3; g = g + 1) begin         // OK
    end
    for (genvar h = 0; h < 3; h = h + 1) begin  // OK
    end
  endgenerate
endmodule
