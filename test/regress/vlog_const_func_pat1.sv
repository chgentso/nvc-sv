module vlog_const_func_pat1;
  typedef struct packed {
    logic [7:0]  idx;
    logic [15:0] start_addr;
    logic [15:0] end_addr;
  } rule_t;

  function rule_t [1:0] make_map();
    for (int unsigned i = 0; i < 2; i++) begin
      make_map[i] = rule_t'{
        idx:        8'(i),
        start_addr: 16'(i * 16'h1000),
        end_addr:   16'((i + 1) * 16'h1000),
        default:    '0
      };
    end
  endfunction

  localparam rule_t [1:0] MAP = make_map();

  initial begin
    if (MAP[0].idx !== 8'd0 || MAP[0].start_addr !== 16'h0000 || MAP[0].end_addr !== 16'h1000 ||
        MAP[1].idx !== 8'd1 || MAP[1].start_addr !== 16'h1000 || MAP[1].end_addr !== 16'h2000) begin
      $display("FAILED: MAP=%h", MAP);
      $finish;
    end
    $display("PASSED");
  end
endmodule
