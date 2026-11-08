// Self-checking. Expected values: IEEE 1800-2017 7.4.? (arrays of queues and of dynamic arrays), 7.10.2
// (push_back, delete), 7.5.1 (new[]), 7.10 (queue assignment copies), 7.5 (an unallocated dynamic array has
// size 0), 21.2.1 (%h of a 28-bit value is not checked as text; the value is compared).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class C;
  int errs = 0;
  logic [27:0] fifos[2][$];
  int dyn[3][];
  function void run();
    fifos[0].push_back(1);
    fifos[1].push_back(2);
    fifos[1].push_back(3);
    dyn[1] = new[2];
    dyn[1][1] = 5;
    `CHK("fifos[0].size", fifos[0].size(), 1)
    `CHK("fifos[1].size", fifos[1].size(), 2)
    `CHK("dyn[1][1]", dyn[1][1], 5)
    `CHK("dyn[0].size", dyn[0].size(), 0)
    fifos[0] = fifos[1];
    fifos[1].delete();
    `CHK("copy size", fifos[0].size(), 2)
    `CHK("deleted size", fifos[1].size(), 0)
    `CHK("fifos[0][1]", fifos[0][1], 28'h3)
    `CHK("fifos[0][0]", fifos[0][0], 28'h2)
    for (int i = 0; i < 2; i++) `CHK("loop size", fifos[i].size(), (i == 0) ? 2 : 0)
  endfunction
endclass
module svarrayq1;
  initial begin automatic C c = new; c.run(); if (c.errs == 0) $display("PASSED"); end
endmodule
