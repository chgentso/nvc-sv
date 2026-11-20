module svstreamcast1;
  typedef bit bit_q_t[$];

  initial begin
    byte unsigned p[$];
    byte unsigned po[$];
    bit bits[$];

    p = {8'h84, 8'haa};
    bits = {<<8{bit_q_t'({<<{p}})}};
    bits.push_front(1'b0);
    po = {<<8{bit_q_t'({<<{bits}})}};

    if (bits.size() !== 17) $fatal(1, "bits.size=%0d", bits.size());
    if (po.size() !== 3) $fatal(1, "po.size=%0d", po.size());
    if (po[0] !== 8'h08 || po[1] !== 8'h55 || po[2] !== 8'h80)
      $fatal(1, "po=%h %h %h", po[0], po[1], po[2]);

    $display("PASSED");
    $finish;
  end
endmodule
