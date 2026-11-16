// $readmemh and $writememh with a hierarchical name of a memory in another module instance
// Expected values: IEEE 1800-2017 21.4 (loading and writing memory arrays from and to files)
module svreadmemhier1_ram #(parameter W = 4);
  localparam depth = 2 ** W;
  logic [7:0] mem [depth];
endmodule

module svreadmemhier1_wrap #(parameter W = 4);
  svreadmemhier1_ram #(.W(W)) ram_i ();
endmodule

module svreadmemhier1;
  svreadmemhier1_wrap #(.W(8)) wrap_i ();
  int f;
  initial begin
    f = $fopen("rmh_a.txt", "w");
    $fwrite(f, "@60\n13\n14\n@ff\nab\n");
    $fclose(f);
    $readmemh("rmh_a.txt", wrap_i.ram_i.mem);
    if (wrap_i.ram_i.mem[8'h60] === 8'h13 && wrap_i.ram_i.mem[8'h61] === 8'h14
        && wrap_i.ram_i.mem[8'hff] === 8'hab) begin
      $writememh("rmh_b.txt", wrap_i.ram_i.mem, 8'h60, 8'h61);
      wrap_i.ram_i.mem[8'h60] = 0;
      $readmemh("rmh_b.txt", wrap_i.ram_i.mem, 8'h60, 8'h61);
      if (wrap_i.ram_i.mem[8'h60] === 8'h13)
        $display("PASSED");
      else
        $error("round trip: %h", wrap_i.ram_i.mem[8'h60]);
    end
    else
      $error("readmemh: %h %h %h", wrap_i.ram_i.mem[8'h60], wrap_i.ram_i.mem[8'h61],
             wrap_i.ram_i.mem[8'hff]);
  end
endmodule
