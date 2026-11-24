set -xe

# Test 1: --2state=off preserves 4-state X/Z and baseline state_bytes
cat >dut_isl.sv <<'EOF'
module sub_isl (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [7:0]  din,
  input  logic [79:0] wide_in,
  input  logic [3:0]  sel,
  output logic [7:0]  q_rst,
  output logic [79:0] wide_out,
  output logic [3:0]  case_out
);
  logic [7:0]  uninit_reg;
  logic [79:0] wide_reg;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      q_rst    <= 8'hA5;
      wide_reg <= 80'h0;
    end else begin
      q_rst    <= din;
      wide_reg <= wide_in ^ 80'h1234_5678_9ABC_DEF0_00FF;
    end
  end

  always_comb begin
    casez (sel)
      4'b1??0: case_out = 4'hA;
      4'b01?1: case_out = 4'h5;
      default: case_out = 4'h3;
    endcase
    wide_out = wide_reg;
  end

  // Dummy read so uninit_reg stays live in the island
  logic _unused_dummy;
  assign _unused_dummy = ^uninit_reg;
endmodule

module tb_isl;
  logic        clk = 0;
  logic        rst_n;
  logic [7:0]  din = 8'h3C;
  logic [79:0] wide_in;
  logic [3:0]  sel;
  logic [7:0]  q_rst;
  logic [79:0] wide_out;
  logic [3:0]  case_out;
  logic [7:0]  tb_ext;

  sub_isl u (
    .clk(clk), .rst_n(rst_n), .din(din), .wide_in(wide_in), .sel(sel),
    .q_rst(q_rst), .wide_out(wide_out), .case_out(case_out)
  );

  initial begin
    // Async reset drops from X -> 0 at t = 0
    rst_n = 1'b0;
    wide_in = {16'hCAFE, 64'hDEADBEEF_12345678};
    sel = 4'b1010;
    #1;
    $display("T1 q_rst=%h case=%h tb_ext=%b unk=%b",
             q_rst, case_out, tb_ext, $isunknown(tb_ext));
    rst_n = 1'b1;
    sel = 4'b0101;
    #5 clk = 1;
    #5 clk = 0;
    #1;
    $display("T2 q_rst=%h case=%h wide=%h tb_ext=%b",
             q_rst, case_out, wide_out, tb_ext);
    $finish;
  end
endmodule
EOF

rm -rf work_off work_isl jcache_m6

nvc --work=work_off -a dut_isl.sv
nvc --work=work_off --rtl-islands=hier --2state=off --dump-islands \
    -e tb_isl >dump_off.txt 2>&1
out_off=$(nvc --work=work_off --rtl-islands=hier --2state=off -r tb_isl)
echo "$out_off"
echo "$out_off" | grep -q "T1 q_rst=a5 case=a tb_ext=xxxxxxxx unk=1"
echo "$out_off" | grep -q "T2 q_rst=3c case=5 wide=d8ca88d52453ccc45687 tb_ext=xxxxxxxx"

# Test 2: --rtl-islands=hier --2state=islands
# Check halved/reduced state_bytes, async reset X->0 -> 0xA5, casez wildcards,
# wide >64-bit boundary X->0, external TB stays X, FST cleanly generated
nvc --work=work_isl -a dut_isl.sv
nvc --work=work_isl --rtl-islands=hier --2state=islands --dump-islands \
    -e tb_isl >dump_isl.txt 2>&1
cat dump_off.txt
cat dump_isl.txt
grep -q "ISLAND 0: .* two_state=[1-9]" dump_isl.txt

sb_off=$(sed -n 's/.*ISLAND 0:.*state_bytes=\([0-9]*\).*/\1/p' dump_off.txt)
sb_isl=$(sed -n 's/.*ISLAND 0:.*state_bytes=\([0-9]*\).*/\1/p' dump_isl.txt)
[ -n "$sb_off" ] && [ -n "$sb_isl" ]
[ "$sb_isl" -lt "$sb_off" ]

out_isl=$(nvc --work=work_isl --rtl-islands=hier --2state=islands \
              -r --wave=m6_isl.fst tb_isl)
echo "$out_isl"
echo "$out_isl" | grep -q "T1 q_rst=a5 case=a tb_ext=xxxxxxxx unk=1"
echo "$out_isl" | grep -q "T2 q_rst=3c case=5 wide=d8ca88d52453ccc45687 tb_ext=xxxxxxxx"
[ -s m6_isl.fst ]

# Also test bare --2state (defaults to islands)
out_bare=$(NVC_2STATE= nvc --work=work_isl --rtl-islands=hier --2state -r tb_isl)
echo "$out_bare" | grep -q "T1 q_rst=a5 case=a tb_ext=xxxxxxxx unk=1"

# Test 3: --rtl-islands=hier --2state=all with mixed VHDL 9-state + SV tristate wire
cat >vhdl_9state.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_9state is
  port (
    s_in  : in  std_logic;
    s_out : out std_logic
  );
end entity;

architecture beh of vhdl_9state is
  signal u_sig : std_logic;
  signal h_sig : std_logic := 'H';
  signal z_sig : std_logic := 'Z';
begin
  process
  begin
    wait for 1 ns;
    report "VHDL9 u=" & std_logic'image(u_sig)
         & " h=" & std_logic'image(h_sig)
         & " z=" & std_logic'image(z_sig)
         & " in=" & std_logic'image(s_in);
    wait;
  end process;

  s_out <= 'Z' when s_in = '1' else 'H';
end architecture;
EOF

cat >sv_all.sv <<'EOF'
typedef struct packed {
  logic [3:0] hi;
  logic [3:0] lo;
} pbyte_t;

module sv_all;
  logic   clk = 0;
  logic   [7:0] sv_var;
  pbyte_t pvar;
  integer ivar;
  logic   s_in = 1'b1;
  logic   s_out_vhdl;
  wire    [7:0] wbus;
  logic   en1 = 0, en2 = 0;
  logic   [7:0] d1 = 8'h5A, d2 = 8'hC3;
  logic   [3:0] cz_in = 4'b1010;
  logic   [3:0] cz_res;
  logic   [3:0] cx_res;
  logic   eq_case;

  assign wbus = en1 ? d1 : 8'hzz;
  assign wbus = en2 ? d2 : 8'hzz;

  vhdl_9state u_vhdl (.s_in(s_in), .s_out(s_out_vhdl));

  always_comb begin
    casez (cz_in)
      4'b1??0: cz_res = 4'h9;
      default: cz_res = 4'h1;
    endcase
    casex (cz_in)
      4'b1xx0: cx_res = 4'h7;
      default: cx_res = 4'h2;
    endcase
    eq_case = (cz_in === 4'b1010) && (8'hzz !== 8'h00);
  end

  initial begin
    #2;
    $display("ALL0 sv_var=%b pvar=%h ivar=%0d unk=%b wbus=%b cz=%h cx=%h eq=%b",
             sv_var, pvar, ivar, $isunknown(sv_var), wbus, cz_res, cx_res, eq_case);
    en1 = 1'b1;
    #2;
    $display("ALL1 wbus=%h (%b)", wbus, wbus);
    en1 = 1'b0;
    en2 = 1'b1;
    #2;
    $display("ALL2 wbus=%h (%x)", wbus, wbus);
    $finish;
  end
endmodule
EOF

rm -rf work_all
nvc --work=work_all --std=2008 -a vhdl_9state.vhd
nvc --work=work_all -a sv_all.sv
nvc --work=work_all --rtl-islands=hier --2state=all -e sv_all
out_all=$(nvc --work=work_all --rtl-islands=hier --2state=all \
              -r --wave=m6_all.fst sv_all 2>&1)
echo "$out_all"
echo "$out_all" | grep -q "VHDL9 u='U' h='H' z='Z' in='1'"
echo "$out_all" | grep -q "ALL0 sv_var=00000000 pvar=00 ivar=0 unk=0 wbus=zzzzzzzz cz=9 cx=7 eq=1"
echo "$out_all" | grep -q "ALL1 wbus=5a"
echo "$out_all" | grep -q "ALL2 wbus=c3"
[ -s m6_all.fst ]

# Test 4: AOT JIT cache isolation across off / islands / all sharing one dir
cat >cache_probe.sv <<'EOF'
module leaf(input logic clk, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk) q <= d;
endmodule

module cache_probe;
  logic clk = 0;
  logic [7:0] uninit_tb;
  logic [7:0] q;
  leaf u (.clk(clk), .d(8'h42), .q(q));
  initial begin
    #1;
    $display("PROBE unk=%b tb=%b q=%b", $isunknown(uninit_tb), uninit_tb, q);
    $finish;
  end
endmodule
EOF

rm -rf work_jc
nvc --work=work_jc -a cache_probe.sv

export NVC_JIT_THRESHOLD=1
export NVC_JIT_ASYNC=0

# Run with off -> tb must be X (unk=1), populate shared cache dir
nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
    --rtl-islands=hier --2state=off -e cache_probe
jc_off=$(nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
             --rtl-islands=hier --2state=off -r cache_probe)
echo "$jc_off"
echo "$jc_off" | grep -q "PROBE unk=1 tb=xxxxxxxx q=xxxxxxxx"

# Same shared cache dir with NVC_2STATE=islands -> q=00000000, tb=xxxxxxxx
nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
    --rtl-islands=hier --2state=islands -e cache_probe
jc_isl=$(NVC_2STATE=islands nvc --work=work_jc --jit-cache=on \
             --jit-cache-dir="$PWD/jcache_m6" --rtl-islands=hier -r cache_probe)
echo "$jc_isl"
echo "$jc_isl" | grep -q "PROBE unk=1 tb=xxxxxxxx q=00000000"

# Same shared cache dir with --2state=all -> tb=00000000 (unk=0), q=00000000
nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
    --rtl-islands=hier --2state=all -e cache_probe
jc_all=$(nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
             --rtl-islands=hier --2state=all -r cache_probe)
echo "$jc_all"
echo "$jc_all" | grep -q "PROBE unk=0 tb=00000000 q=00000000"

# Re-run --2state=off against warm cache with all 3 cached -> still must return X!
nvc --work=work_jc --jit-cache=on --jit-cache-dir="$PWD/jcache_m6" \
    --rtl-islands=hier --2state=off -e cache_probe
jc_off2=$(NVC_JIT_LOG=1 nvc --work=work_jc --jit-cache=on \
              --jit-cache-dir="$PWD/jcache_m6" --rtl-islands=hier --2state=off \
              -r cache_probe 2>&1)
echo "$jc_off2"
echo "$jc_off2" | grep -q "PROBE unk=1 tb=xxxxxxxx q=xxxxxxxx"
echo "$jc_off2" | grep -q "\[cache hit\]"

# Test 5: SVA / external observer on FF-written vs comb-written island signals
# under --2state=islands and --2state=all, plus 4-state wire ternary into 2-state var
cat >sva_m6.sv <<'EOF'
module sender_m6 #(parameter int W = 80) (
  input  logic         clk,
  input  logic         rst_n,
  input  wire          en_w,
  input  logic         set_i,
  output logic         state_o,
  output logic [W-1:0] cnt_o
);
  logic         set_q;
  logic [W-1:0] cnt_q, cnt_d;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      set_q <= 1'b0;
      cnt_q <= '0;
    end else begin
      set_q <= set_i;
      cnt_q <= cnt_d;
    end
  end

  assign state_o = set_q;
  assign cnt_d = en_w ? (cnt_q + {{(W-1){1'b0}}, 1'b1}) : cnt_q;
  assign cnt_o = cnt_q;

  property eq_p;
    @(posedge clk) set_q === state_o;
  endproperty
  assert property (eq_p) else $fatal(1, "SVA_FAIL eq_p q=%b o=%b", set_q, state_o);
endmodule

module tb_sva_m6;
  logic        clk = 0;
  logic        rst_n = 0;
  wire         en_w;
  logic        en_drv = 0;
  logic        set_i = 0;
  logic        state_o;
  logic [79:0] cnt_o;
  logic        seen_q;

  assign en_w = en_drv;
  sender_m6 u (
    .clk(clk), .rst_n(rst_n), .en_w(en_w), .set_i(set_i),
    .state_o(state_o), .cnt_o(cnt_o)
  );
  assign seen_q = u.set_q;

  always #5 clk = ~clk;

  initial begin
    #1;
    if (seen_q !== 1'b0 || state_o !== 1'b0)
      $fatal(1, "T0_FAIL seen_q=%b state_o=%b", seen_q, state_o);
    @(posedge clk);
    rst_n = 1'b1;
    en_drv = 1'b1;
    set_i = 1'b1;
    @(posedge clk);
    #1;
    $display("SVA_PASS seen=%b state=%b cnt=%0d", seen_q, state_o, cnt_o);
    $finish;
  end
endmodule
EOF

rm -rf work_sva_isl work_sva_all
nvc --work=work_sva_isl -a sva_m6.sv
nvc --work=work_sva_isl --rtl-islands=hier --2state=islands -e tb_sva_m6
out_sva_isl=$(nvc --work=work_sva_isl --rtl-islands=hier --2state=islands -r tb_sva_m6)
echo "$out_sva_isl"
echo "$out_sva_isl" | grep -q "SVA_PASS seen=1 state=1 cnt=1"

nvc --work=work_sva_all -a sva_m6.sv
nvc --work=work_sva_all --rtl-islands=hier --2state=all -e tb_sva_m6
out_sva_all=$(nvc --work=work_sva_all --rtl-islands=hier --2state=all -r tb_sva_m6)
echo "$out_sva_all"
echo "$out_sva_all" | grep -q "SVA_PASS seen=1 state=1 cnt=1"

# Test 6: Island calling 4-state package function with fixed-size unpacked array arg
cat >pkg_uarr_m6.sv <<'EOF'
package pkg_uarr_m6;
  function automatic logic [7:0] xor_arr(logic [7:0] x, logic [7:0] tbl [4]);
    logic [7:0] acc;
    acc = x;
    for (int i = 0; i < 4; i++)
      acc ^= tbl[i];
    return acc;
  endfunction
endpackage

module dut_uarr_m6(input logic [7:0] din, output logic [7:0] dout);
  import pkg_uarr_m6::*;
  localparam logic [7:0] TBL [4] = '{8'h11, 8'h22, 8'h44, 8'h88};
  always_comb begin
    dout = xor_arr(din, TBL);
  end
endmodule

module tb_uarr_m6;
  logic [7:0] din = 8'h0F, dout;
  dut_uarr_m6 u (.din(din), .dout(dout));
  initial begin
    #1;
    $display("UARR dout=%h", dout);
    $finish;
  end
endmodule
EOF

rm -rf work_uarr_isl
nvc --work=work_uarr_isl -a pkg_uarr_m6.sv
nvc --work=work_uarr_isl --rtl-islands=hier --2state=islands -e tb_uarr_m6
out_uarr=$(nvc --work=work_uarr_isl --rtl-islands=hier --2state=islands -r tb_uarr_m6)
echo "$out_uarr"
echo "$out_uarr" | grep -q "UARR dout=f0"
