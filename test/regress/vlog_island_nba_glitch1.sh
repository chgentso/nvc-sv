set -xe

# Each non-blocking assignment is an update event of its own (IEEE 1800
# 4.9.4, 10.4.2): a register of an SV island assigned more than once in a
# time step that changes and changes back wakes processes and island
# domains waiting for a change or an edge, as with NVC_RTL_ISLANDS=off.
# VHDL signal assignments in one process overwrite the projected waveform
# instead and give no event.

cat >glitch1.sv <<'EOF'
module glitch1;
  logic clk = 0;
  logic q = 0, v = 0, p = 0;
  logic [7:0] w = 0, x = 3;
  logic [3:0] x4 = 0;
  logic [99:0] wd = 0;
  logic [3:0] ca = 0, cb = 0;
  logic [3:0] req = 4'b0100;
  logic [1:0] idx = 0;
  logic [7:0] pw = 0;
  int nq = 0, np = 0, nw = 0, nx4 = 0, nwd = 0, nca = 0, nidx = 0, npw = 0;
  logic [7:0] pos = 0, neg = 0;

  always_ff @(posedge clk) begin        // Island domain
    q <= ~v;
    q <= v;
    p <= 1;
    p <= 0;
    w <= x + 1;
    if (x[0]) w <= w;
    x4 <= 'x;
    x4 <= 0;
    wd <= ~wd;
    wd <= wd;
    {ca, cb} <= {~ca, cb + 4'd1};
    ca <= ca;
    idx <= 0;
    for (int i = 0; i < 4; i++)
      if (req[i]) idx <= i[1:0];
    pw[0] <= 1;
    pw[0] <= 0;
    pw[7:4] <= pw[7:4] + 1;
  end

  always_ff @(posedge p) pos <= pos + 1;   // Clocked by a glitching register
  always_ff @(negedge p) neg <= neg + 1;

  always @(q) nq++;                     // Not in the island
  always @(posedge p) np++;
  always @(w) nw++;
  always @(x4) nx4++;
  always @(wd) nwd++;
  always @(ca) nca++;
  always @(idx) nidx++;
  always @(posedge pw[0]) npw++;

  initial begin
    repeat (4) begin
      #1 clk = 1;
      #1 clk = 0;
      x = x + 1;
    end
    #1 $display("nq=%0d np=%0d nw=%0d nx4=%0d nwd=%0d nca=%0d nidx=%0d npw=%0d",
                nq, np, nw, nx4, nwd, nca, nidx, npw);
    $display("q=%b p=%b w=%0d x4=%h wd=%h ca=%h cb=%h idx=%0d pw=%h",
             q, p, w, x4, wd, ca, cb, idx, pw);
    $display("pos=%0d neg=%0d", pos, neg);
    $finish;
  end
endmodule
EOF

cat >glitch1.vhd <<'EOF'
library ieee;
use ieee.std_logic_1164.all;

entity glitch1_vhd is
end entity;

architecture test of glitch1_vhd is
    signal clk : std_logic := '0';
    signal q, v : std_logic := '0';
    signal n : natural := 0;
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            q <= not v;
            q <= v;
        end if;
    end process;

    p_count : process is
    begin
        wait on q;
        n <= n + 1;
    end process;

    p_stim : process is
    begin
        for i in 1 to 4 loop
            wait for 1 ns;
            clk <= '1';
            wait for 1 ns;
            clk <= '0';
        end loop;
        wait for 1 ns;
        report "n=" & natural'image(n) & " q=" & std_logic'image(q);
        wait;
    end process;
end architecture;
EOF

strip_date() {
   sed '/^\$date/,/^\$end/d' "$1" > "$2"
}

run_mode() {
   mkdir -p $1
   ( cd $1
     export NVC_RTL_ISLANDS=$2 NVC_ISLAND_FAST=$3
     nvc -a ../glitch1.sv -e glitch1
     NVC_DUMP_ISLANDS=1 nvc -r --wave=w.vcd --format=vcd glitch1 >out.txt \
                     2>err.txt
     strip_date w.vcd w.norm
     sed '/ISLAND/d' out.txt > out.norm
     sed '/ISLAND/d' err.txt > err.norm
     nvc --std=2008 -a ../glitch1.vhd -e glitch1_vhd
     NVC_DUMP_ISLANDS=1 nvc -r --wave=v.vcd --format=vcd glitch1_vhd \
                     >vout.txt 2>verr.txt
     strip_date v.vcd v.norm
     sed '/ISLAND/d' vout.txt > vout.norm
     sed '/ISLAND/d' verr.txt > verr.norm )
}

run_mode fast hier 1
run_mode nofast hier 0
run_mode off off 0

grep -q 'ISLAND_HIER: islands=[1-9]' fast/out.txt
grep -q 'VHDL_ISLAND_ELAB: .* islands=[1-9]' fast/vout.txt

for m in fast nofast; do
   for f in out.norm err.norm w.norm vout.norm verr.norm v.norm; do
      diff -u off/$f $m/$f
   done
done

grep -q 'nq=4 np=4 nw=4 nx4=4 nwd=4 nca=4 nidx=4 npw=4' off/out.norm
grep -q 'pos=4 neg=4' off/out.norm
grep -q 'n=0' off/verr.norm
