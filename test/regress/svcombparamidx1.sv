// An always_comb process that reads a parameter array element selected by
// a variable is sensitive to the variable (IEEE 1800-2017 9.2.2.2.1): the
// implicit sensitivity list holds every expression read, including the
// indices of a select of a constant.
module svcombparamidx1;
  typedef struct packed { int unsigned kind; bit en; } appcfg_t;
  localparam appcfg_t AppCfg [3] = '{ '{0, 1'b1}, '{1, 1'b0}, '{2, 1'b1} };
  localparam int Table [4] = '{10, 20, 30, 40};
  logic [1:0] app_id = 0;
  appcfg_t app_cfg;
  int looked_up;
  int part;
  int errs = 0;

  always_comb app_cfg = AppCfg[app_id];
  always_comb looked_up = Table[app_id];
  always_comb part = {Table[app_id], 8'h0} >> 8;

  initial begin
    for (int i = 0; i < 3; i++) begin
      app_id = i[1:0];
      #1;
      if (app_cfg.kind != i || app_cfg.en !== (i != 1)) begin
        errs++;
        $error("id %0d: kind %0d en %b", i, app_cfg.kind, app_cfg.en);
      end
      if (looked_up != 10 * (i + 1) || part != 10 * (i + 1)) begin
        errs++;
        $error("id %0d: table %0d part %0d", i, looked_up, part);
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
