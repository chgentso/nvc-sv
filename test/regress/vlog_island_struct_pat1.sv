typedef enum logic [1:0] {
  PRIV_U = 2'b00,
  PRIV_S = 2'b01,
  PRIV_M = 2'b11
} priv_e;

typedef struct packed {
  logic  uie;
  logic  mie;
  logic  upie;
  logic  mpie;
  priv_e mpp;
  logic  mprv;
} status_t;

typedef struct packed {
  logic [3:0] ver;
  logic [2:0] cause;
  priv_e      prv;
  logic [2:0] pad;
} dcsr_t;

module struct_pat_dut (
  input  logic    clk,
  input  logic    rst_n,
  input  logic    set_mie,
  input  logic    set_mpie,
  input  logic    in_err,
  output status_t status_o,
  output dcsr_t   dcsr_o,
  output status_t comb_o
);
  status_t status_q, status_n;
  dcsr_t   dcsr_q;

  always_comb begin
    status_n = '{
      uie:  1'b0,
      mie:  set_mie,
      upie: 1'b0,
      mpie: set_mpie,
      mpp:  PRIV_M,
      mprv: in_err
    };
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      status_q <= '{
        uie:  1'b0,
        mie:  1'b0,
        upie: 1'b0,
        mpie: 1'b0,
        mpp:  PRIV_M,
        mprv: 1'b0
      };
      dcsr_q <= '{
        ver:   4'h4,
        cause: 3'h0,
        prv:   PRIV_M,
        default: '0
      };
    end else begin
      status_q <= '{
        uie:  1'b0,
        mie:  status_n.mie,
        upie: 1'b0,
        mpie: status_n.mpie,
        mpp:  PRIV_M,
        mprv: 1'b0
      };
      dcsr_q <= '{
        ver:   4'h4,
        cause: in_err ? 3'h3 : 3'h1,
        prv:   status_n.mpp,
        default: '0
      };
    end
  end

  assign status_o = status_q;
  assign dcsr_o   = dcsr_q;
  assign comb_o   = '{ default: 1'b0, mie: status_q.mie, mpp: status_q.mpp };
endmodule

module vlog_island_struct_pat1;
  logic    clk = 0;
  logic    rst_n = 0;
  logic    set_mie = 0;
  logic    set_mpie = 0;
  logic    in_err = 0;
  status_t status_o, comb_o;
  dcsr_t   dcsr_o;

  struct_pat_dut dut (.*);

  always #5 clk = ~clk;

  initial begin
    #2;
    if (status_o.mpp !== PRIV_M || status_o.mie !== 1'b0 || status_o.mpie !== 1'b0)
      $fatal(1, "rst status mismatch: %b", status_o);
    if (dcsr_o.ver !== 4'h4 || dcsr_o.cause !== 3'h0 || dcsr_o.prv !== PRIV_M || dcsr_o.pad !== 3'h0)
      $fatal(1, "rst dcsr mismatch: %b", dcsr_o);
    if (comb_o.mie !== 1'b0 || comb_o.mpp !== PRIV_M || comb_o.mprv !== 1'b0)
      $fatal(1, "rst comb mismatch: %b", comb_o);

    rst_n = 1;
    set_mie = 1;
    set_mpie = 0;
    in_err = 1;
    @(posedge clk); #1;
    if (status_o.mie !== 1'b1 || status_o.mpie !== 1'b0 || status_o.mprv !== 1'b0 || status_o.mpp !== PRIV_M)
      $fatal(1, "cyc1 status mismatch: %b", status_o);
    if (dcsr_o.ver !== 4'h4 || dcsr_o.cause !== 3'h3 || dcsr_o.prv !== PRIV_M || dcsr_o.pad !== 3'h0)
      $fatal(1, "cyc1 dcsr mismatch: %b", dcsr_o);
    if (comb_o.mie !== 1'b1 || comb_o.mpp !== PRIV_M || comb_o.mpie !== 1'b0)
      $fatal(1, "cyc1 comb mismatch: %b", comb_o);

    set_mie = 0;
    set_mpie = 1;
    in_err = 0;
    @(posedge clk); #1;
    if (status_o.mie !== 1'b0 || status_o.mpie !== 1'b1 || status_o.mpp !== PRIV_M)
      $fatal(1, "cyc2 status mismatch: %b", status_o);
    if (dcsr_o.cause !== 3'h1 || dcsr_o.pad !== 3'h0)
      $fatal(1, "cyc2 dcsr mismatch: %b", dcsr_o);
    if (comb_o.mie !== 1'b0 || comb_o.mpp !== PRIV_M)
      $fatal(1, "cyc2 comb mismatch: %b", comb_o);

    $display("PASSED");
    $finish;
  end
endmodule
