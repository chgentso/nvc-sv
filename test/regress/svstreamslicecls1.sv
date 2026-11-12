// A slice of a fixed size unpacked array is the same operand of a
// streaming concatenation whether the array is a module variable or a
// property of a class (IEEE 1800-2017 11.4.14 and 7.4.3).
module svstreamslicecls1;
  localparam int N = 40;
  bit [31:0] mod_a [N];
  int errs = 0;

  class holder;
    bit [31:0] a [N];
    logic [127:0] up, down, range;
    task get(int base);
      up = {<<32 {a[base +: 4]}};
      down = {<<32 {a[(base + 3) -: 4]}};
      range = {<<32 {a[5 : 8]}};
    endtask
  endclass

  initial begin
    automatic holder h = new;
    logic [127:0] want, want5, m_up, m_down, m_range;
    for (int i = 0; i < N; i++) begin
      mod_a[i] = 32'h100 + i;
      h.a[i] = 32'h100 + i;
    end

    want5 = {32'h108, 32'h107, 32'h106, 32'h105};
    for (int base = 0; base <= N - 4; base += 9) begin
      want = {32'h100 + base + 3, 32'h100 + base + 2, 32'h100 + base + 1, 32'h100 + base};
      m_up = {<<32 {mod_a[base +: 4]}};
      m_down = {<<32 {mod_a[(base + 3) -: 4]}};
      m_range = {<<32 {mod_a[5 : 8]}};
      h.get(base);
      if (m_up !== want || m_down !== want || m_range !== want5) begin
        errs++;
        $error("module array base %0d: %h %h %h want %h", base, m_up, m_down, m_range, want);
      end
      if (h.up !== want || h.down !== want || h.range !== want5) begin
        errs++;
        $error("class property base %0d: %h %h %h want %h", base, h.up, h.down, h.range, want);
      end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
