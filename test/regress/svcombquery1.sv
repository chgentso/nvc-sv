module svcombquery1 #(
  parameter type addr_t = logic [31:0]
);
  addr_t a = 32'h1234_5678;
  addr_t b = 32'h1234_0000;
  logic hit;

  always_comb begin
    hit = (a[$bits(addr_t)-1:16] == b[$bits(addr_t)-1:16]);
  end

  initial begin
    #1;
    if (hit !== 1'b1) $fatal(1, "hit1=%b exp=1", hit);
    b = 32'h9999_0000;
    #1;
    if (hit !== 1'b0) $fatal(1, "hit2=%b exp=0", hit);
    $display("PASSED");
    $finish;
  end
endmodule
