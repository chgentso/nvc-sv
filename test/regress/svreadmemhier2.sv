// $readmemh into a memory of another instance named by an absolute path
// from the top module, or from inside a generate block
module svreadmemhier2_rom #(parameter W = 39, parameter D = 4);
  logic [W-1:0] mem [D];
endmodule

module svreadmemhier2;
  svreadmemhier2_rom u_rom ();
  int f;
  initial begin
    f = $fopen("rmh2.txt", "w");
    $fwrite(f, "@0 7FFFFFFFFF\n@1 0123456789\n");
    $fclose(f);
    $readmemh("rmh2.txt", u_rom.mem);
    $display("relative %h %h", u_rom.mem[0], u_rom.mem[1]);
    u_rom.mem[0] = '0;
    $readmemh("rmh2.txt", svreadmemhier2.u_rom.mem);
    $display("absolute %h", u_rom.mem[0]);
  end
  if (1) begin : gen
    initial begin
      #1 u_rom.mem[0] = '0;
      $readmemh("rmh2.txt", u_rom.mem);
      $display("generate relative %h", u_rom.mem[0]);
      u_rom.mem[0] = '0;
      $readmemh("rmh2.txt", svreadmemhier2.u_rom.mem);
      $display("generate absolute %h", u_rom.mem[0]);
    end
  end
endmodule
