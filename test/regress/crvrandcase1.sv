// randcase draws from the active process random stream (srandom / randstate)
module crvrandcase1;
  function automatic int draw_seq();
    int acc;
    int pick;
    acc = 0;
    for (int i = 0; i < 16; i++) begin
      randcase
        1: pick = 0;
        1: pick = 1;
        1: pick = 2;
        1: pick = 3;
      endcase
      acc = (acc * 4) + pick;
    end
    return acc;
  endfunction

  initial begin
    process p;
    string st;
    int s1, s2, s3, s4, s5;
    int fa, fb;

    p = process::self();

    // srandom reproduces the randcase sequence
    p.srandom(12345);
    s1 = draw_seq();
    p.srandom(12345);
    s2 = draw_seq();
    if (s1 != s2)
      $fatal(1, "srandom did not reproduce randcase: %0d vs %0d", s1, s2);

    // A different seed gives a different sequence
    p.srandom(54321);
    s3 = draw_seq();
    if (s3 == s1)
      $fatal(1, "different srandom gave identical randcase: %0d", s3);

    // get_randstate / set_randstate restores the randcase stream
    st = p.get_randstate();
    s4 = draw_seq();
    p.set_randstate(st);
    s5 = draw_seq();
    if (s4 != s5)
      $fatal(1, "set_randstate did not restore randcase: %0d vs %0d", s4, s5);

    // Two forked processes seeded identically draw the same randcase sequence
    fork
      begin
        process pa;
        pa = process::self();
        pa.srandom(777);
        fa = draw_seq();
      end
      begin
        process pb;
        pb = process::self();
        pb.srandom(777);
        fb = draw_seq();
      end
    join
    if (fa != fb)
      $fatal(1, "forked processes with same srandom differed: %0d vs %0d", fa, fb);

    $display("PASSED");
  end
endmodule
