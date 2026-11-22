module svstdrand2;
  typedef logic [127:0] token_t;

  function automatic token_t get_random_token();
    if (!std::randomize(get_random_token) with {
          get_random_token[15:0] == 16'hA55A;
          get_random_token[127:120] == 8'h3C;
        })
      $fatal(1, "randomize failed");
  endfunction

  initial begin
    automatic token_t t = get_random_token();
    if (t[15:0] !== 16'hA55A || t[127:120] !== 8'h3C) begin
      $display("FAIL: t=%h", t);
      $finish;
    end
    $display("PASSED");
  end
endmodule
