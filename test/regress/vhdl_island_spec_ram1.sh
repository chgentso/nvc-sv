set -xe

# Speculative Mode A row clean tracking (NVC_ISLAND_SPEC=1): a memory keeps
# one clean byte per row, so an island whose memory has rows never written,
# written in part or written with 'X' data still runs Mode A; a load of
# such a row, or a partial store into it, deopts and Mode B computes with
# the metavalues.  The same design is run with a small memory (an ordinary
# 2D signal) and a large one (a sparse RAM written through the log).
# Covered: a read of an unwritten row ('U' reaches the output, a register,
# an unfused process and the VCD), a nibble write then reads, 'X' data,
# whole writes making rows clean, two nibble stores into a clean row and a
# whole plus a nibble store into an unwritten row in one cycle (in Mode A),
# reads of clean rows (the island stays Mode A with held rows), and a full
# waveform dump of the memories.  Output and VCD must be those of Mode B
# and --rtl-islands=off, also with NVC_ISLAND_SPEC_STRESS and
# NVC_ISLAND_ACT_CHECK.

cat >ram.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- A memory of DEPTH rows with a write port for whole rows (WE) and one for
-- nibbles (WN, BE(i) enables nibble i), an asynchronous and a registered
-- read port
entity ram_e is
    generic ( DEPTH : positive );
    port ( clk, we, wn : in std_logic;
           be          : in std_logic_vector(1 downto 0);
           wa, ra      : in natural range 0 to DEPTH - 1;
           d           : in std_logic_vector(7 downto 0);
           q, qr       : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of ram_e is
    type mem_t is array (0 to DEPTH - 1) of std_logic_vector(7 downto 0);
    signal mem : mem_t;
    signal cnt : unsigned(7 downto 0) := x"00";
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            cnt <= cnt + 1;
            if we = '1' then
                mem(wa) <= d;
            end if;
            if wn = '1' then
                for i in 0 to 1 loop
                    if be(i) = '1' then
                        mem(wa)(4 * i + 3 downto 4 * i) <= d(4 * i + 3 downto 4 * i);
                    end if;
                end loop;
            end if;
            qr <= mem(ra) xor std_logic_vector(cnt);
        end if;
    end process;
    q <= mem(ra);
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ram_tb is end entity;

architecture test of ram_tb is
    signal clk, we, wn : std_logic := '0';
    signal be : std_logic_vector(1 downto 0) := "00";
    signal wa, ra, wa2, ra2 : natural range 0 to 3 := 0;
    signal wa3, ra3 : natural range 0 to 31 := 0;
    signal d : std_logic_vector(7 downto 0) := x"00";
    signal q, qr, q3, qr3 : std_logic_vector(7 downto 0);
    signal mon_q, mon_qr, mon_q3, mon_qr3 : std_logic_vector(7 downto 0);
begin
    -- 4 x 8 bits: an ordinary 2D signal; 32 x 8 bits: a sparse RAM
    u_r  : entity work.ram_e generic map (4)
        port map (clk, we, wn, be, wa, ra, d, q, qr);
    u_r3 : entity work.ram_e generic map (32)
        port map (clk, we, wn, be, wa3, ra3, d, q3, qr3);

    -- Not fused (wait statement): what the VCD records
    mon : process is
    begin
        wait on q, qr, q3, qr3;
        mon_q <= q;
        mon_qr <= qr;
        mon_q3 <= q3;
        mon_qr3 <= qr3;
    end process;

    stim : process is
        procedure tick is
        begin
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end procedure;
    begin
        for k in 0 to 399 loop
            d <= std_logic_vector(to_unsigned((k * 37 + 11) mod 256, 8));
            we <= '0';
            wn <= '0';
            be <= "00";
            -- Rows 0 and 1 written whole early on, row 2 low nibble only,
            -- row 3 with 'X' data then later whole; both nibbles of clean
            -- row 0 in one cycle; a whole and a nibble store into an
            -- unwritten row in one cycle; reads of row 0/1 for a long
            -- time, of rows 2/3 for a few cycles each
            case k is
                when 2 => we <= '1'; wa <= 0; wa3 <= 0;
                when 3 => we <= '1'; wa <= 1; wa3 <= 17;
                when 5 => wn <= '1'; be <= "01"; wa <= 2; wa3 <= 5;
                when 7 => we <= '1'; wa <= 3; wa3 <= 30; d <= "0101X101";
                when 150 => we <= '1'; wa <= 3; wa3 <= 30;
                when 160 => wn <= '1'; be <= "01"; wa <= 2; wa3 <= 5;
                when 170 => we <= '1'; wa <= 2; wa3 <= 5;
                when 300 => wn <= '1'; be <= "11"; wa <= 0; wa3 <= 0;
                when 310 => we <= '1'; wn <= '1'; be <= "10"; wa <= 1;
                            wa3 <= 9; d <= x"3c";
                when others => null;
            end case;
            if k = 1 or (k >= 60 and k < 63) or k = 152 or k = 162 then
                ra <= 2; ra3 <= 5;
            elsif k = 80 or k = 81 then
                ra <= 3; ra3 <= 30;
            elsif k = 100 then
                ra <= 2; ra3 <= 9;    -- not written yet
            elsif k = 315 or k = 316 then
                ra <= 1; ra3 <= 9;
            elsif k mod 2 = 0 then
                ra <= 0; ra3 <= 0;
            else
                ra <= 1; ra3 <= 17;
            end if;
            tick;
            report integer'image(k) & " q=" & to_string(q) & " qr="
                & to_string(qr) & " q3=" & to_string(q3) & " qr3="
                & to_string(qr3);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS
unset NVC_ISLAND_SPEC_STRESS NVC_ISLAND_SPEC_RT

nd() { sed '/^\$date/,/\$end/d' $1; }
flt() { grep -v -e '^\*\* Debug' -e 'not dumped by default'; }

nvc --std=2008 --work=w_off -a ram.vhd
nvc --std=2008 --work=w_off --rtl-islands=off -e ram_tb \
    -r --wave=off.vcd --format=vcd 2>&1 | flt > off.log
nvc --std=2008 --work=w_off -r --wave=offfull.vcd --format=vcd \
    --dump-arrays ram_tb > /dev/null 2>&1
nvc --std=2008 --work=w_b -a ram.vhd
nvc --std=2008 --work=w_b --rtl-islands=hier -e ram_tb \
    -r --wave=b.vcd --format=vcd 2>&1 | flt > b.log
diff -u off.log b.log
diff -u <(nd off.vcd) <(nd b.vcd)

export NVC_ISLAND_SPEC=1
# Promote holding values at once, not after the default wait
export NVC_ISLAND_SPEC_HOLD_WAIT=0
nvc --std=2008 --work=w_s -a ram.vhd
nvc --std=2008 --work=w_s --rtl-islands=hier -e ram_tb
for st in 0 1 2 5; do
    NVC_ISLAND_SPEC_STRESS=$st NVC_ISLAND_ACT_CHECK=1 NVC_ISLAND_SPEC_STATS=2 \
        nvc --std=2008 --work=w_s -r --wave=s$st.vcd --format=vcd ram_tb \
        2>&1 | flt > s${st}_raw.log
    grep -v '^ISLAND_SPEC' s${st}_raw.log > s$st.log
    diff -u off.log s$st.log
    diff -u <(nd off.vcd) <(nd s$st.vcd)
done
NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 --work=w_s -r --no-island-fast \
    ram_tb 2>&1 | flt > nf_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' nf_raw.log)
NVC_ISLAND_SPEC_STATS=1 nvc --std=2008 --work=w_s -r --wave=full.vcd \
    --format=vcd --dump-arrays ram_tb 2>&1 | flt \
    > full_raw.log
diff -u off.log <(grep -v '^ISLAND_SPEC' full_raw.log)
diff -u <(nd offfull.vcd) <(nd full.vcd)

cat s0_raw.log full_raw.log | grep ISLAND_SPEC
# Unwritten, nibble-written and 'X' rows reach the outputs
grep -q ' 1 q=UUUUUUUU qr=UUUUUUUU q3=UUUUUUUU qr3=UUUUUUUU' off.log
grep -q ' 61 q=UUUU0100 qr=UUUU1001 q3=UUUU0100 qr3=UUUU1001' off.log
grep -q ' 80 q=0101X101 qr=0000X101 q3=0101X101 qr3=0000X101' off.log
grep -q ' 100 q=UUUU0100 qr=UUUU0000 q3=UUUUUUUU qr3=UUUUUUUU' off.log
grep -q ' 315 q=00111100 qr=00000111 q3=00111100 qr3=00000111' off.log
# Both memories have a variant, end in Mode A (the sparse RAM with rows
# never written) and deopted on unclean rows
grep -q '^ISLAND_SPEC_RT: ticks=[0-9]* variants=2 promoted=2 in_a=2 ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: island=1 mode=A .* held_rows=[1-9][0-9]* ' s0_raw.log
grep -q '^ISLAND_SPEC_RT: deopts .* row=[1-9]' s0_raw.log
