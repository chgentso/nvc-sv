module svforceconcat1;
  reg [3:0] a;
  reg [7:0] b;
  wire [3:0] wa = 4'h1;
  wire [3:0] wb = 4'h2;

  initial begin
    a = 4'h0;
    b = 8'h00;

    force {a, b} = 12'ha5c;
    #1;
    if (a !== 4'ha || b !== 8'h5c) $fatal(1, "forced a=%h b=%h", a, b);

    a = 4'h3;
    b = 8'h44;
    #1;
    if (a !== 4'ha || b !== 8'h5c) $fatal(1, "still forced a=%h b=%h", a, b);

    release {a, b};
    #1;
    if (a !== 4'ha || b !== 8'h5c) $fatal(1, "after release a=%h b=%h", a, b);

    a = 4'h7;
    b = 8'h89;
    #1;
    if (a !== 4'h7 || b !== 8'h89) $fatal(1, "reassigned a=%h b=%h", a, b);

    force {wa, wb} = 8'hf0;
    #1;
    if (wa !== 4'hf || wb !== 4'h0) $fatal(1, "forced wires %h %h", wa, wb);
    release {wa, wb};
    #1;
    if (wa !== 4'h1 || wb !== 4'h2) $fatal(1, "released wires %h %h", wa, wb);

    $display("PASSED");
    $finish;
  end
endmodule
