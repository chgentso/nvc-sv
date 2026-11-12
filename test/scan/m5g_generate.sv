module leaf #(parameter int W = 1) (input logic a);
endmodule

module top;
  parameter N = 2;
  logic x;
  genvar i, j;
  for (i = 0; i < N; i++) begin : g
    leaf u (.a(x));
    for (j = 0; j < 2; j++) begin : h
      leaf v (.a(x));
    end
  end
  if (N > 1) begin : yes
    leaf c1 (.a(x));
  end
  else begin : no
    leaf c2 (.a(x));
  end
  case (N)
    1: leaf c3 (.a(x));
    2: begin : two
      leaf c4 (.a(x));
    end
    default: begin : other
      leaf c5 (.a(x));
    end
  endcase
  if (N == 2)
    leaf c6 (.a(x));
  if (N == 2) begin
    leaf c7 (.a(x));
  end
  for (i = 0; i < 1; i++)
    leaf c8 (.a(x));
endmodule
