// Expected values: IEEE 1800-2017 27.4/27.5 (generate scopes, parameters, ports with unpacked array
// connections), 9.4.2 (posedge/negedge), 10.3 (continuous assignment), 11.4.6 (bitwise XOR).
// clk toggles every 5 ns from 0: posedges at 5,15,25; negedges at 10,20,30. rdy rises at 12.
// d = {3, c}: w = d[0]+1 = 4; loc of gen_i[1] = d[1]^w = c^4 = 8.
// The port net may see a z/x->0 transition at time 0 (race with the always blocks), so only
// edges after time 0 are counted.
// The always blocks count their executions and check the sampled values themselves.
`timescale 1ns/1ns
module sub #(parameter int N = 2, parameter bit EN = 1) (input clk_i, input rdy_i, input [3:0] d_i [2], output logic v_o);
  logic [3:0] w;
  int np = 0, nn = 0, bad = 0;
  assign v_o = rdy_i;
  assign w = d_i[0] + 4'd1;
  if (EN) begin : gen_p
    always @(posedge clk_i) if ($time > 0) begin
      np++;
      if (rdy_i !== (($time >= 12) ? 1'b1 : 1'b0) || d_i[0] !== 4'h3 || w !== 4'h4) begin
        $error("p t=%0t rdy=%b d0=%h w=%h", $time, rdy_i, d_i[0], w); bad++;
      end
    end
    for (genvar i = 0; i < N; i++) begin : gen_i
      logic [3:0] loc;
      assign loc = d_i[i] ^ w;
      if (i == 1) begin : gen_n
        always @(negedge clk_i) if ($time > 0) begin
          nn++;
          if (rdy_i !== (($time >= 12) ? 1'b1 : 1'b0) || loc !== 4'h8 || w !== 4'h4 || N != 2) begin
            $error("n%0d t=%0t rdy=%b loc=%h w=%h N=%0d", i, $time, rdy_i, loc, w, N); bad++;
          end
        end
      end
    end
  end
endmodule
module svgenscopedisp1;
  logic clk = 0, rdy = 0;
  logic [3:0] d [2];
  logic v;
  sub #(.N(2), .EN(1)) u(.clk_i(clk), .rdy_i(rdy), .d_i(d), .v_o(v));
  always #5 clk = ~clk;
  initial begin
    d[0] = 4'h3; d[1] = 4'hc;
    #12 rdy = 1;
    #20;
    if (u.np != 3 || u.nn != 3 || u.bad != 0 || v !== 1'b1) begin
      $error("np=%0d nn=%0d bad=%0d v=%b", u.np, u.nn, u.bad, v);
      $display("FAILED");
    end else $display("PASSED");
    $finish;
  end
endmodule
