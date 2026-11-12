// Parts of a variable that are driven by continuous assignments or by
// output ports of generate blocks, where the position of each part is
// an element of a parameter array or a member of a structure in one
// (IEEE 1800-2017 6.5, 11.5.1): the parts do not overlap so a variable
// may have a driver for each.
module svpartparam1_part(output logic [15:0] d);
  assign d = 16'hA5C3;
endmodule

module svpartparam1;
  typedef struct packed { int unsigned offset; int unsigned size; } info_t;
  typedef struct { int unsigned offset; int unsigned size; } uinfo_t;
  localparam info_t Info [3] = '{ '{0, 1}, '{1, 2}, '{3, 1} };
  localparam uinfo_t UInfo [3] = '{ '{offset: 0, size: 1}, '{offset: 1, size: 2},
                                    '{offset: 3, size: 1} };
  localparam int Off [3] = '{0, 1, 3};

  logic [3:0][7:0] a, b, c;

  for (genvar k = 0; k < 3; k++) begin : g
    if (Info[k].size == 2) begin : gw
      svpartparam1_part u(.d(a[Info[k].offset +: Info[k].size]));
    end
    else begin : gb
      assign a[Info[k].offset +: Info[k].size] = 8'h10 + 8'(k);
    end

    assign b[UInfo[k].offset +: UInfo[k].size] = {UInfo[k].size{8'h20 + 8'(k)}};
    assign c[Off[k]] = 8'h30 + 8'(k);
  end

  initial begin
    #1;
    if (a !== 32'h12a5c310) $fatal(1, "a = %h", a);
    if (b !== 32'h2221_2120) $fatal(1, "b = %h", b);
    if (c[3] !== 8'h32 || c[1] !== 8'h31 || c[0] !== 8'h30) $fatal(1, "c = %h", c);
    $display("PASSED");
  end
endmodule
