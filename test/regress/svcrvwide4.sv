// Self-checking: elements of a fixed rand array of wide values and a wide packed two dimensional array
// compared with fixed values in a conditional constraint, with the other element left free.
// Expected values: IEEE 1800-2017 18.4 (rand arrays), 18.5 (constraints), 18.5.6 (if-else constraints),
// 7.4 (packed and unpacked arrays).
class Item;
  rand bit [7:0][31:0] key [2];
  rand bit [3:0][31:0] iv;
  bit fixed_key_en, fixed_iv_en;
  bit [7:0][31:0] fixed_key [2] = '{256'h0000111122223333444455556666777788889999AAAABBBBCCCCDDDDEEEEFFFF, 256'h0};
  bit [3:0][31:0] fixed_iv = 128'h0123456789abcdef0011223344556677;
  constraint key_c { if (fixed_key_en) { key[0] == fixed_key[0]; key[1] == fixed_key[1]; } }
  constraint iv_c { if (fixed_iv_en) { iv == fixed_iv; } }
endclass

module svcrvwide4;
  Item it = new;
  int bad, differ0, differ1;
  bit [255:0] prev0, prev1;
  bit [127:0] piv;
  initial begin
    for (int i = 0; i < 20; i++) begin
      if (!it.randomize()) begin $error("randomize failed"); bad++; end
      else begin
        if (i > 0 && it.key[0] != prev0) differ0++;
        if (i > 0 && it.key[1] != prev1) differ1++;
        if (i > 0 && it.iv != piv) differ1++;
        prev0 = it.key[0]; prev1 = it.key[1]; piv = it.iv;
      end
    end
    if (differ0 < 15 || differ1 < 15) begin $error("free values did not vary %0d %0d", differ0, differ1); bad++; end
    it.fixed_key_en = 1;
    it.fixed_iv_en = 1;
    for (int i = 0; i < 10; i++) begin
      if (!it.randomize()) begin $error("randomize failed with fixed values"); bad++; end
      else if (it.key[0] != it.fixed_key[0] || it.key[1] != 0 || it.iv != it.fixed_iv) begin
        $error("key %h %h iv %h", it.key[0], it.key[1], it.iv); bad++;
      end
    end
    if (bad == 0) $display("PASSED");
  end
endmodule
