// Disjoint continuous drivers and output port connections indexed by
// struct-parameter members, parameter slices, and $bits expressions.
// IEEE 1800-2017 11.5.1 (longest static prefix)
module svdriveconst1_out #(parameter int W = 8) (
  input  logic [W-1:0] d_i,
  output logic [W-1:0] d_o
);
  assign d_o = d_i;
endmodule

module svdriveconst1_conc #(
  parameter logic [3:0] PAT = 4'b1011
) (
  input  logic [3:0] i,
  output logic [2:0] o
);
  if (PAT[0])
    assign o[0] = i[0];

  for (genvar j = 1; j < 4; j++) begin : g
    if (PAT[j])
      assign o[(($bits(PAT[j-1:0]) < 65) ? 0 : 0)
               + ((0 < $bits(PAT[j-1:0])) ? ((PAT[j-1:0] >> 0) & 1'b1) : 0)
               + ((1 < $bits(PAT[j-1:0])) ? ((PAT[j-1:0] >> 1) & 1'b1) : 0)
               + ((2 < $bits(PAT[j-1:0])) ? ((PAT[j-1:0] >> 2) & 1'b1) : 0)] = i[j];
  end
endmodule

module svdriveconst1;
  typedef struct packed {
    int unsigned NoSlv;
    int unsigned NoMst;
  } cfg_t;

  localparam cfg_t Cfg = '{ NoSlv: 2, NoMst: 2 };

  typedef struct packed {
    logic [7:0] data;
  } resp_t;

  resp_t [Cfg.NoSlv-1:0][Cfg.NoMst:0] slv_resps;

  for (genvar i = 0; i < Cfg.NoSlv; i++) begin : g_demux
    svdriveconst1_out #(.W(8)) u_err (
      .d_i (8'hA0 + i[7:0]),
      .d_o (slv_resps[i][Cfg.NoMst].data)
    );
    for (genvar j = 0; j < Cfg.NoMst; j++) begin : g_cross
      assign slv_resps[i][j] = '{ data: 8'(i * 16 + j) };
    end
  end

  logic [3:0] cin = 4'b1011;
  logic [2:0] cout;
  svdriveconst1_conc u_conc (.i(cin), .o(cout));

  initial begin
    #1;
    if (slv_resps[0][0].data === 8'h00 && slv_resps[0][1].data === 8'h01
        && slv_resps[0][2].data === 8'hA0
        && slv_resps[1][0].data === 8'h10 && slv_resps[1][1].data === 8'h11
        && slv_resps[1][2].data === 8'hA1
        && cout === 3'b111)
      $display("PASSED");
    else
      $fatal(1, "mismatch: %h %h %h / %h %h %h / %b",
             slv_resps[0][0].data, slv_resps[0][1].data, slv_resps[0][2].data,
             slv_resps[1][0].data, slv_resps[1][1].data, slv_resps[1][2].data,
             cout);
  end
endmodule
