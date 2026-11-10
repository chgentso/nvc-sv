// Self-checking: a rand packed structure with members wider than 64 bits, constrained through the members
// and an inline randomize with, the members of one object assigned to another with a constraint.
// Expected values: IEEE 1800-2017 7.2.1 (packed structures), 18.4, 18.5 (constraints on members),
// 18.7 (randomize with).
typedef struct packed {
  logic valid;
  logic [1:0][255:0] key;
  logic [7:0] tag;
} key_req_t;

class Item;
  rand bit valid;
  rand bit [255:0] key0, key1;
endclass

class SideSeq;
  rand key_req_t req;
endclass

class Pick;
  rand bit [1:0] idx;
  bit [3:0][127:0] v = {128'h1, 128'h2, 128'h1_0000_0000_0000_0000_00ab, 128'h4};
  rand bit [0:2][127:0] up;
  constraint c { v[idx] == 128'h1_0000_0000_0000_0000_00ab; up[1] == 128'h5 + 128'h1_0000_0000_0000_0000; }
endclass

module svcrvwide5;
  Pick pk = new;
  SideSeq s = new;
  Item it = new;
  int bad, differ;
  bit [511:0] prev;
  initial begin
    for (int i = 0; i < 40; i++) begin
      if (!s.randomize() with { req.valid == (i % 2 == 0); }) begin $error("randomize failed"); bad++; end
      else begin
        if (s.req.valid !== (i % 2 == 0)) begin $error("valid %b", s.req.valid); bad++; end
        if (i > 0 && s.req.key != prev) differ++;
        prev = s.req.key;
        if (!it.randomize() with { valid == s.req.valid; key0 == s.req.key[0]; key1 == s.req.key[1]; }) begin
          $error("item randomize failed"); bad++;
        end
        else if (it.valid !== s.req.valid || it.key0 !== s.req.key[0] || it.key1 !== s.req.key[1]) begin
          $error("copy mismatch %h %h", it.key0, s.req.key[0]); bad++;
        end
      end
    end
    if (differ < 30) begin $error("keys do not vary: %0d", differ); bad++; end
    for (int i = 0; i < 10; i++) begin
      if (!s.randomize() with { req.key[1][255:240] == 16'hcafe; req.key[0] != 256'h0; req.tag inside {[8'd1:8'd9]}; })
        begin $error("constrained randomize failed"); bad++; end
      else if (s.req.key[1][255:240] !== 16'hcafe || s.req.key[0] === 256'h0 || !(s.req.tag inside {[1:9]})) begin
        $error("key %h tag %0d", s.req.key, s.req.tag); bad++;
      end
    end
    for (int i = 0; i < 20; i++) begin
      if (!pk.randomize()) begin $error("pick randomize failed"); bad++; end
      else if (pk.idx != 1 || pk.v[pk.idx] !== 128'h1_0000_0000_0000_0000_00ab
               || pk.up[1] !== 128'h1_0000_0000_0000_0005) begin
        $error("pick idx %0d v %h up %h", pk.idx, pk.v, pk.up); bad++;
      end
    end
    if (bad == 0) $display("PASSED");
  end
endmodule
