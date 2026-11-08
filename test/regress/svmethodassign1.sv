`timescale 1ns/1ns
// Continuous assignment of the result of a method call on a queue
// Expected values: IEEE 1800-2017 10.3 (a continuous assignment evaluates its right-hand side at time 0 and again
// whenever an operand changes), 7.10.2 (size, push_back, pop_front)
module svmethodassign1;
  int fifos[2][$];
  int q[$];
  int fails = 0;
  wire ef0, ef1, big;
  assign ef0 = fifos[0].size() == 0;
  assign ef1 = fifos[1].size() == 0;
  assign big = q.size() > 1;
  initial begin
    #1 if ({ef0, ef1, big} !== 3'b110) begin $error("t=1: got %b %b %b", ef0, ef1, big); fails++; end
    fifos[0].push_back(5);
    q.push_back(1); q.push_back(2);
    #1 if ({ef0, ef1, big} !== 3'b011) begin $error("t=2: got %b %b %b", ef0, ef1, big); fails++; end
    fifos[0].pop_front();
    #1 if ({ef0, ef1, big} !== 3'b111) begin $error("t=3: got %b %b %b", ef0, ef1, big); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
