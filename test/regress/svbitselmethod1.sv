module svbitselmethod1;
  int q[$];
  logic b;
  logic [7:0] s;

  initial begin
    q.push_back(32'h10ef);
    b = q.sum()[4];
    if (b !== 1'b0) $fatal(1, "bit 4=%b", b);
    b = q.sum()[5];
    if (b !== 1'b1) $fatal(1, "bit 5=%b", b);
    s = q.sum()[11:4];
    if (s !== 8'h0e) $fatal(1, "slice=%h", s);
    s = q.sum()[4+:8];
    if (s !== 8'h0e) $fatal(1, "pos slice=%h", s);
    s = q.sum()[11-:8];
    if (s !== 8'h0e) $fatal(1, "neg slice=%h", s);
    $display("PASSED");
    $finish;
  end
endmodule
