module sv_gen_class1;
  for (genvar i = 0; i < 3; i = i + 1) begin : g
    class C;
      int value;
      function new(int base);
        this.value = base + i;
      endfunction
    endclass
    C c = new(10);
  end

  initial begin
    if (g[0].c.value !== 10 || g[1].c.value !== 11 || g[2].c.value !== 12)
      $fatal(1, "got %0d %0d %0d", g[0].c.value, g[1].c.value, g[2].c.value);
    $display("PASS");
  end
endmodule
