module svstdrand1;
  typedef struct {
    bit [5:0] a;
    bit [5:0] b;
  } delays_t;

  task automatic test_local_struct();
    delays_t d;
    bit [5:0] seen_a, seen_b;
    seen_a = 0;
    seen_b = 0;
    for (int i = 0; i < 8; i++) begin
      if (!std::randomize(d))
        $fatal(1, "randomize local struct failed");
      seen_a |= d.a;
      seen_b |= d.b;
    end
    if (seen_a == 0 || seen_b == 0)
      $fatal(1, "local struct fields never randomized");
  endtask

  task automatic test_local_wide_array();
    bit [7:0][31:0] key[2];
    bit [255:0] or0, or1;
    or0 = '0;
    or1 = '0;
    for (int i = 0; i < 4; i++) begin
      if (!std::randomize(key))
        $fatal(1, "randomize local wide array failed");
      or0 |= key[0];
      or1 |= key[1];
    end
    if (or0 == '0 || or1 == '0)
      $fatal(1, "local wide array never randomized");
  endtask

  task automatic test_port_struct(inout delays_t d);
    if (!std::randomize(d))
      $fatal(1, "randomize port struct failed");
  endtask

  initial begin
    delays_t pd;
    bit [5:0] seen_pa;
    test_local_struct();
    test_local_wide_array();
    seen_pa = 0;
    for (int i = 0; i < 8; i++) begin
      test_port_struct(pd);
      seen_pa |= pd.a;
    end
    if (seen_pa == 0)
      $fatal(1, "port struct never randomized");
    $display("PASSED");
    $finish;
  end
endmodule
