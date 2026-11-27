module vlog_gen_case_if_sub #(parameter int P = 1) (output int o);
  assign o = blk.f(P);

  generate
    case (P)
      1, 2:
        if (P == 1) begin : blk
          int w = 10;
          function automatic int f(input int x);
            return x + 100;
          endfunction
        end
        else begin : blk
          int w = 20;
          function automatic int f(input int x);
            return x + 200;
          endfunction
        end
      default: begin : blk
        int w = 30;
        function automatic int f(input int x);
          return x + 300;
        endfunction
      end
    endcase
  endgenerate
endmodule

module vlog_gen_case_if;
  int o1, o2, o3;
  vlog_gen_case_if_sub #(1) u1 (.o(o1));
  vlog_gen_case_if_sub #(2) u2 (.o(o2));
  vlog_gen_case_if_sub #(3) u3 (.o(o3));

  initial begin
    #1;
    if (o1 !== 101 || o2 !== 202 || o3 !== 303)
      $fatal(1, "o1=%0d o2=%0d o3=%0d", o1, o2, o3);
    if (u1.blk.w !== 10 || u2.blk.w !== 20 || u3.blk.w !== 30)
      $fatal(1, "w=%0d %0d %0d", u1.blk.w, u2.blk.w, u3.blk.w);
    $display("PASSED");
  end
endmodule
