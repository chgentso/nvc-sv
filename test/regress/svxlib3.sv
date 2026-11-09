module svxlib3;
  logic clk = 0;
  logic [7:0] q;
  bus_if bus (clk);
  virtual bus_if vif;

  sender s (bus);
  receiver r (bus, q);

  initial begin
    vif = bus;
    #1;
    if (vif.data !== 8'h3C || q !== 8'h3C) $fatal(1, "bad data");
    $display("PASS %h", q);
  end
endmodule
