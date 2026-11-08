// Unpacked array parameters and variables initialised with a concatenation, used in generate loops
// Expected values: IEEE 1800-2017 10.10 (unpacked array concatenation), 6.20.2, 27.4 (generate loop),
// 21.3.3 ($sformatf), 7.4
`timescale 1ns/1ns
package pk;
  parameter string LIST[2] = {"fatal_fault", "other"};
  parameter string ONE[1] = {"alpha"};
  parameter int  NUMS[3] = '{3, 4, 5};
  parameter int  CAT[3] = {7, 8, 9};
  parameter int  NUM = 2;
endpackage
module svparamarrcat1;
  import pk::*;
  localparam string LP[2] = {"lp0", "lp1"};
  int v[3] = {1, 2, 3};
  string s[2] = {"v0", "v1"};
  bit [3:0] b[2] = {4'h1, 4'h2};
  int errs = 0;
  string gpath[NUM];
  int gn[NUM], gc[NUM];
  for (genvar k = 0; k < NUM; k++) begin : g
    initial begin
      automatic string path = $sformatf("*.env.m_agent_%0s", LIST[k]);
      gpath[k] = path; gn[k] = NUMS[k]; gc[k] = CAT[k];
    end
  end
  initial begin
    automatic string t = ONE[0];
    #1;
    if (gpath[0] != "*.env.m_agent_fatal_fault" || gn[0] != 3 || gc[0] != 7) begin
      $error("k=0: %s %0d %0d", gpath[0], gn[0], gc[0]); errs++;
    end
    if (gpath[1] != "*.env.m_agent_other" || gn[1] != 4 || gc[1] != 8) begin
      $error("k=1: %s %0d %0d", gpath[1], gn[1], gc[1]); errs++;
    end
    if (t != "alpha" || LP[0] != "lp0" || LP[1] != "lp1") begin $error("strings: %s %s %s", t, LP[0], LP[1]); errs++; end
    if (v[0] != 1 || v[1] != 2 || v[2] != 3) begin $error("int concat"); errs++; end
    if (s[0] != "v0" || s[1] != "v1" || b[0] !== 4'h1 || b[1] !== 4'h2) begin $error("string/bit concat"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
