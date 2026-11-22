module svretpat1;
  typedef struct packed {
    logic [7:0] a;
    logic [7:0] b;
  } pair_t;

  function automatic pair_t make_pair(logic [7:0] x, logic [7:0] y);
    return ('{a: x, b: y});
  endfunction

  function automatic pair_t choose_pair(bit sel);
    return sel ? ('{a: 8'h12, b: 8'h34}) : ('{a: 8'h56, b: 8'h78});
  endfunction

  initial begin
    automatic pair_t p1 = make_pair(8'haa, 8'h55);
    automatic pair_t p2 = choose_pair(1'b1);
    automatic pair_t p3 = choose_pair(1'b0);
    if (p1.a !== 8'haa || p1.b !== 8'h55)
      $fatal(1, "p1=%h", p1);
    if (p2.a !== 8'h12 || p2.b !== 8'h34)
      $fatal(1, "p2=%h", p2);
    if (p3.a !== 8'h56 || p3.b !== 8'h78)
      $fatal(1, "p3=%h", p3);
    $display("PASSED");
    $finish;
  end
endmodule
