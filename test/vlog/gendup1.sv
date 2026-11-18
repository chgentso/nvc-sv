module gendup1;
  parameter X = 2;
  begin : block
  end
  begin : block
  end
  if (X > 0) begin : block1
  end
  if (X > 1) begin : block1
  end
  if (X > 2) begin : ok
  end else begin : ok
  end
endmodule
