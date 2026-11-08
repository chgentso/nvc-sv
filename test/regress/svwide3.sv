// Wide (72..256 bit) elements of queues and dynamic arrays held in class properties and modules.
// Expected values: IEEE 1800-2017 7.10 (queues), 7.5 (dynamic arrays, new[n](old)), 8.5 (class
// properties), 11.4.10 (shift), 11.4.12 (concatenation), 11.5.1 (part select), 6.8 (2-state
// elements start at 0).
class C;
  bit [127:0] q[$];
  logic [255:0] d[];
  bit [71:0] s[$];
  function void add(bit [127:0] x); q.push_back(x); endfunction
  function bit [127:0] get(int i); return q[i]; endfunction
  function void set(int i, bit [127:0] x); q[i] = x; endfunction
endclass
module svwide3;
  bit [127:0] q[$];
  bit [127:0] d[];
  C c;
  bit [127:0] x;
  int errors = 0;
  initial begin
    c = new;
    c.add(128'h11); c.add(128'h22);
    c.q[0] = 128'h123456789ABCDEF0123456789ABCDEF0;
    c.set(1, 128'hFEDCBA9876543210FEDCBA9876543210);
    if (c.q[0] !== 128'h123456789abcdef0123456789abcdef0 || c.q[1] !== 128'hfedcba9876543210fedcba9876543210 ||
        c.get(1) !== 128'hfedcba9876543210fedcba9876543210) begin
      $error("1: %h %h %h", c.q[0], c.q[1], c.get(1)); errors++;
    end
    c.d = new[2];
    c.d[0] = 256'hFF00FF00FF00FF00FF00FF00FF00FF0011223344556677889900AABBCCDDEEFF;
    c.d[1] = c.d[0] >> 8;
    if (c.d[0] !== 256'hff00ff00ff00ff00ff00ff00ff00ff0011223344556677889900aabbccddeeff) begin $error("d0 %h", c.d[0]); errors++; end
    if (c.d[1] !== 256'h00ff00ff00ff00ff00ff00ff00ff00ff0011223344556677889900aabbccddee) begin $error("d1 %h", c.d[1]); errors++; end
    c.s.push_back(72'hAABBCCDDEEFF001122);
    c.s[0] = 72'h123456789ABCDEF012;
    c.s.push_back(c.s[0]+1);
    if (c.s[0] !== 72'h123456789abcdef012 || c.s[1] !== 72'h123456789abcdef013) begin
      $error("s: %h %h", c.s[0], c.s[1]); errors++;
    end
    q = '{128'h1, 128'h2};
    q[0] = q[1] << 70;
    x = q[0];
    if (q[0] !== 128'h00000000000000800000000000000000 || q[1] !== 128'h2 ||
        x !== 128'h00000000000000800000000000000000) begin
      $error("q: %h %h %h", q[0], q[1], x); errors++;
    end
    d = new[2]; d[1] = 128'h1 << 100; d[0] = d[1]; d = new[3](d);
    if (d[0] !== 128'h00000010000000000000000000000000 || d[1] !== 128'h00000010000000000000000000000000 ||
        d[2] !== 128'h0) begin
      $error("d: %h %h %h", d[0], d[1], d[2]); errors++;
    end
    q[1] = {64'h1, 64'h2};
    q[0] = {q[1][63:0], q[1][127:64]};
    if (q[0] !== 128'h00000000000000020000000000000001 || q[1] !== 128'h00000000000000010000000000000002) begin
      $error("swap: %h %h", q[0], q[1]); errors++;
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
