// An element of an associative array of queues is a set of values in an
// inside constraint (IEEE 1800-2017 18.5.4 and 11.4.13): the variable takes
// one of the values of that queue.
module svrandinsideaq1;
  typedef enum logic [3:0] { S0, S1, S2, S3, S4 } st_e;
  const st_e NEXT [st_e][$] = '{ S0: {S1, S2}, S1: {S3}, S2: {S3, S4}, S3: {S4}, S4: {S0} };

  initial begin
    automatic st_e cur = S2, nxt;
    automatic int seen3 = 0, seen4 = 0, bad = 0;
    for (int i = 0; i < 40; i++) begin
      if (!std::randomize(nxt) with { nxt inside {NEXT[cur]}; }) bad++;
      else if (nxt == S3) seen3++;
      else if (nxt == S4) seen4++;
      else bad++;
    end
    if (bad != 0 || seen3 == 0 || seen4 == 0)
      $fatal(1, "bad %0d seen3 %0d seen4 %0d", bad, seen3, seen4);

    cur = S1;
    for (int i = 0; i < 10; i++) begin
      if (!std::randomize(nxt) with { nxt inside {NEXT[cur]}; } || nxt != S3)
        $fatal(1, "single value queue: %s", nxt.name());
    end
    $display("PASSED");
  end
endmodule
