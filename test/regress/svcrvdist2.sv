// Self-checking test of `dist` constraints under implication, `solve ... before`, and a queue whose size is
// constrained by another random variable.
// Expected values: IEEE 1800-2017 18.5.4 (dist: weight := per value, probabilities proportional to the
// weights), 18.5.10 (implication), 18.5.9 (solve before: does not change the solution space), 18.5.8 /
// 7.10 (queue size constraint).  Statistical checks with wide tolerances: with burst == WRAP, len is 15
// with probability 0.9 (300 draws: more than 240 expected, mean 270, sigma 5) and 0 with probability 0.1
// (fewer than 60, mean 30); with burst == INCR all nine listed lengths appear (each has probability 1/9,
// 300 draws) and len > 100 (probability 2/9, mean 67, sigma 7) occurs more than 30 times.
module svcrvdist2;
  typedef enum bit [1:0] {FIXED, INCR, WRAP} burst_e;
  class tx;
    rand bit [7:0] len;
    rand burst_e burst;
    rand bit [15:0] data [$:256];
    constraint c0 { solve burst before len; }
    constraint c1 { (burst == FIXED || burst == WRAP) -> len dist {0:=1, 15:=9}; }
    constraint c2 { (burst == INCR) -> len dist {0:=1, 1:=1, 3:=1, 7:=1, 15:=1, 31:=1,
                                                 63:=1, 127:=1, 255:=1}; }
    constraint c3 { data.size() == len + 1; }
  endclass
  initial begin
    automatic tx x = new;
    automatic int hi_cnt = 0, lo_cnt = 0, bad = 0, big = 0, bigsum = 0, fails = 0;
    automatic bit [8:0] seen = 0;
    repeat (300) begin
      if (!x.randomize() with { burst == WRAP; }) bad++;
      else if (x.len == 15) hi_cnt++;
      else if (x.len == 0) lo_cnt++;
      else bad++;
    end
    if (!(hi_cnt > 240) || !(lo_cnt < 60) || bad != 0) begin
      fails++; $error("wrap: hi=%0d lo=%0d bad=%0d", hi_cnt, lo_cnt, bad);
    end
    repeat (300) begin
      if (!x.randomize() with { burst == INCR; }) bad++;
      else begin
        if (x.len > 100) big++;
        case (x.len)
          0: seen[0] = 1;  1: seen[1] = 1;  3: seen[2] = 1;
          7: seen[3] = 1;  15: seen[4] = 1; 31: seen[5] = 1;
          63: seen[6] = 1; 127: seen[7] = 1; 255: seen[8] = 1;
          default: bad++;
        endcase
        if (x.data.size() != x.len + 1) bad++;
      end
    end
    if (seen != 9'b111111111 || !(big > 30) || bad != 0) begin
      fails++; $error("incr: seen=%b big=%0d bad=%0d", seen, big, bad);
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
