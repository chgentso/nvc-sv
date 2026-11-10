// Self-checking: a value bounded by a variable which implications fix for each member of an enum
// (reduced from a keyed hash block size constraint).  Randomization must succeed, every strength must be
// reachable and the value must lie in the range of the block size of its strength.
// Expected values: IEEE 1800-2017 18.5.6 (implication), 18.5.3 (set membership, here inside with a variable
// bound), 18.4 (rand enum variables), 18.5.10 (solve before).
package hpkg;
  typedef enum logic [2:0] { L128 = 3'b000, L224 = 3'b001, L256 = 3'b010, L384 = 3'b011, L512 = 3'b100 } strength_e;
  typedef enum logic [1:0] { Sha3 = 2'b00, Shake = 2'b10, CShake = 2'b11 } mode_e;
endpackage

class Vseq;
  rand bit kmac_en;
  rand bit xof_en;
  rand hpkg::mode_e hash_mode;
  rand hpkg::strength_e strength;
  rand int unsigned output_len;
  rand int unsigned keccak_block_size;
  rand bit err;
  bit enable_full_kmac = 1;

  constraint xof_en_c { (!kmac_en) -> (xof_en == 1'b0); }
  constraint hash_mode_c {
    if (enable_full_kmac) {
      if (kmac_en) { hash_mode == hpkg::CShake; } else { hash_mode != hpkg::CShake; }
    }
  }
  constraint output_len_sha3_c {
    if (hash_mode == hpkg::Sha3) {
      (strength == hpkg::L224) -> (output_len == 28);
      (strength == hpkg::L256) -> (output_len == 32);
      (strength == hpkg::L384) -> (output_len == 48);
      (strength == hpkg::L512) -> (output_len == 64);
    }
  }
  constraint strength_c {
    solve err before hash_mode;
    solve err before strength;
    if (err) {
      (hash_mode inside {hpkg::Shake, hpkg::CShake}) -> !(strength inside {hpkg::L128, hpkg::L256});
      (hash_mode == hpkg::Sha3) -> (strength == hpkg::L128);
    } else {
      (hash_mode inside {hpkg::Shake, hpkg::CShake}) -> (strength inside {hpkg::L128, hpkg::L256});
      (hash_mode == hpkg::Sha3) -> (strength != hpkg::L128);
    }
  }
  constraint keccak_block_size_c {
    (strength == hpkg::L128) -> (keccak_block_size == 168);
    (strength == hpkg::L224) -> (keccak_block_size == 144);
    (strength == hpkg::L256) -> (keccak_block_size == 136);
    (strength == hpkg::L384) -> (keccak_block_size == 104);
    (strength == hpkg::L512) -> (keccak_block_size == 72);
  }
  constraint output_len_c { output_len inside {[1:keccak_block_size]}; }
endclass

module svcrvimpl2;
  Vseq v = new;
  int bad, seen[5], n_ok;
  initial begin
    for (int i = 0; i < 300; i++) begin
      if (!v.randomize()) begin $error("randomize failed at %0d", i); bad++; end
      else begin
        int want;
        case (v.strength)
          hpkg::L128: want = 168;
          hpkg::L224: want = 144;
          hpkg::L256: want = 136;
          hpkg::L384: want = 104;
          hpkg::L512: want = 72;
          default: want = -1;
        endcase
        if (v.keccak_block_size != want) begin $error("block size %0d for strength %0d", v.keccak_block_size, v.strength); bad++; end
        if (v.output_len < 1 || v.output_len > v.keccak_block_size) begin
          $error("output_len %0d block %0d", v.output_len, v.keccak_block_size); bad++;
        end
        if (v.hash_mode == hpkg::Sha3 && v.strength != hpkg::L128) begin
          int exp_len;
          case (v.strength)
            hpkg::L224: exp_len = 28;
            hpkg::L256: exp_len = 32;
            hpkg::L384: exp_len = 48;
            default: exp_len = 64;
          endcase
          if (v.output_len != exp_len) begin $error("sha3 output_len %0d strength %0d", v.output_len, v.strength); bad++; end
        end
        seen[v.strength]++;
        n_ok++;
      end
    end
    // With no error injection both strengths of the XOF modes and the three others of Sha3 are reachable
    foreach (seen[s]) if (seen[s] == 0) begin $error("strength %0d never chosen: %p", s, seen); bad++; end
    if (bad == 0) $display("PASSED");
  end
endmodule
