// Dimension order of mixed associative / queue / dynamic / fixed unpacked arrays.
// Expected values: IEEE 1800-2017 7.4.5 (multidimensional arrays: the first
// dimension written is the leftmost), 7.5 (dynamic arrays), 7.8 (associative
// arrays), 7.10 (queues), 11.5.1 (bit and part select of an element), 12.7.3 (foreach).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdimorder1;
  bit [7:0] sd[int][2];
  int qa[$][3];
  int da[][2];
  int fq[2][$];
  bit [3:0] mid[2][string][2];
  int err = 0;
  int visits, sum;
  initial begin
    sd[9][1] = 5;
    sd[9][0][1] = 1'b1;
    sd[4][1][7:4] = 4'hA;
    `CHK(sd.size() == 2, "sd.size")
    `CHK(sd[9][1] === 8'd5, "sd[9][1]")
    `CHK(sd[9][0] === 8'b00000010, "sd[9][0]")
    `CHK(sd[4][1] === 8'hA0, "sd[4][1]")
    `CHK(sd[4][0] === 8'h00, "sd[4][0]")
    qa.push_back('{1, 2, 3});
    qa.push_back('{4, 5, 6});
    qa[1][2] = 60;
    `CHK(qa.size() == 2, "qa.size")
    `CHK(qa[0][1] == 2, "qa[0][1]")
    `CHK(qa[1][2] == 60, "qa[1][2]")
    `CHK(qa[1][0] == 4, "qa[1][0]")
    da = new[2];
    da[1][1] = 7;
    `CHK(da.size() == 2, "da.size")
    `CHK(da[1][1] == 7, "da[1][1]")
    `CHK(da[0][0] == 0, "da[0][0]")
    fq[1].push_back(3);
    `CHK(fq[1].size() == 1, "fq[1].size")
    `CHK(fq[1][0] == 3, "fq[1][0]")
    `CHK(fq[0].size() == 0, "fq[0].size")
    mid[1]["k"][1] = 4'd9;
    `CHK(mid[1].size() == 1, "mid[1].size")
    `CHK(mid[1]["k"][1] == 9, "mid[1][k][1]")
    // foreach visits every (key, j) pair once; the order is not checked
    foreach (sd[k, j]) begin
      visits++;
      sum += sd[k][j];
      `CHK(k == 4 || k == 9, "foreach key")
      `CHK(j == 0 || j == 1, "foreach j")
    end
    `CHK(visits == 4, "foreach visits")
    `CHK(sum == 160 + 2 + 5, "foreach sum")
    if (err == 0) $display("PASSED");
  end
endmodule
