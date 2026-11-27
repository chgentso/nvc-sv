set -xe

# Speculative Mode A: a vector compared with an integer constant that does
# not fit the vector (unsigned < 300 with 8 bits, signed = -129, ...) has a
# result fixed by numeric_std without a warning, so the Mode A variant folds
# it and no longer takes the conversion deopt stub on every call.  The
# output must stay the one of Mode B, for every relational operator, for the
# integer on either side and for constants above and below the range.

cat >fold.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fold_e is
    port ( clk : in std_logic; u : in unsigned(7 downto 0);
           s : in signed(7 downto 0);
           q : out std_logic_vector(119 downto 0) );
end entity;
architecture rtl of fold_e is
    signal rr : std_logic_vector(119 downto 0) := (others => '0');
begin
    process (clk) is
    begin
        if rising_edge(clk) then
            rr(0) <= '1' when u < 300 else '0';
            rr(1) <= '1' when 300 < u else '0';
            rr(2) <= '1' when u <= 300 else '0';
            rr(3) <= '1' when 300 <= u else '0';
            rr(4) <= '1' when u > 300 else '0';
            rr(5) <= '1' when 300 > u else '0';
            rr(6) <= '1' when u >= 300 else '0';
            rr(7) <= '1' when 300 >= u else '0';
            rr(8) <= '1' when u = 300 else '0';
            rr(9) <= '1' when 300 = u else '0';
            rr(10) <= '1' when u /= 300 else '0';
            rr(11) <= '1' when 300 /= u else '0';
            rr(12) <= '1' when u < 256 else '0';
            rr(13) <= '1' when 256 < u else '0';
            rr(14) <= '1' when u <= 256 else '0';
            rr(15) <= '1' when 256 <= u else '0';
            rr(16) <= '1' when u > 256 else '0';
            rr(17) <= '1' when 256 > u else '0';
            rr(18) <= '1' when u >= 256 else '0';
            rr(19) <= '1' when 256 >= u else '0';
            rr(20) <= '1' when u = 256 else '0';
            rr(21) <= '1' when 256 = u else '0';
            rr(22) <= '1' when u /= 256 else '0';
            rr(23) <= '1' when 256 /= u else '0';
            rr(24) <= '1' when u < 255 else '0';
            rr(25) <= '1' when 255 < u else '0';
            rr(26) <= '1' when u <= 255 else '0';
            rr(27) <= '1' when 255 <= u else '0';
            rr(28) <= '1' when u > 255 else '0';
            rr(29) <= '1' when 255 > u else '0';
            rr(30) <= '1' when u >= 255 else '0';
            rr(31) <= '1' when 255 >= u else '0';
            rr(32) <= '1' when u = 255 else '0';
            rr(33) <= '1' when 255 = u else '0';
            rr(34) <= '1' when u /= 255 else '0';
            rr(35) <= '1' when 255 /= u else '0';
            rr(36) <= '1' when u < 70000 else '0';
            rr(37) <= '1' when 70000 < u else '0';
            rr(38) <= '1' when u <= 70000 else '0';
            rr(39) <= '1' when 70000 <= u else '0';
            rr(40) <= '1' when u > 70000 else '0';
            rr(41) <= '1' when 70000 > u else '0';
            rr(42) <= '1' when u >= 70000 else '0';
            rr(43) <= '1' when 70000 >= u else '0';
            rr(44) <= '1' when u = 70000 else '0';
            rr(45) <= '1' when 70000 = u else '0';
            rr(46) <= '1' when u /= 70000 else '0';
            rr(47) <= '1' when 70000 /= u else '0';
            rr(48) <= '1' when s < 128 else '0';
            rr(49) <= '1' when 128 < s else '0';
            rr(50) <= '1' when s <= 128 else '0';
            rr(51) <= '1' when 128 <= s else '0';
            rr(52) <= '1' when s > 128 else '0';
            rr(53) <= '1' when 128 > s else '0';
            rr(54) <= '1' when s >= 128 else '0';
            rr(55) <= '1' when 128 >= s else '0';
            rr(56) <= '1' when s = 128 else '0';
            rr(57) <= '1' when 128 = s else '0';
            rr(58) <= '1' when s /= 128 else '0';
            rr(59) <= '1' when 128 /= s else '0';
            rr(60) <= '1' when s < 127 else '0';
            rr(61) <= '1' when 127 < s else '0';
            rr(62) <= '1' when s <= 127 else '0';
            rr(63) <= '1' when 127 <= s else '0';
            rr(64) <= '1' when s > 127 else '0';
            rr(65) <= '1' when 127 > s else '0';
            rr(66) <= '1' when s >= 127 else '0';
            rr(67) <= '1' when 127 >= s else '0';
            rr(68) <= '1' when s = 127 else '0';
            rr(69) <= '1' when 127 = s else '0';
            rr(70) <= '1' when s /= 127 else '0';
            rr(71) <= '1' when 127 /= s else '0';
            rr(72) <= '1' when s < -128 else '0';
            rr(73) <= '1' when -128 < s else '0';
            rr(74) <= '1' when s <= -128 else '0';
            rr(75) <= '1' when -128 <= s else '0';
            rr(76) <= '1' when s > -128 else '0';
            rr(77) <= '1' when -128 > s else '0';
            rr(78) <= '1' when s >= -128 else '0';
            rr(79) <= '1' when -128 >= s else '0';
            rr(80) <= '1' when s = -128 else '0';
            rr(81) <= '1' when -128 = s else '0';
            rr(82) <= '1' when s /= -128 else '0';
            rr(83) <= '1' when -128 /= s else '0';
            rr(84) <= '1' when s < -129 else '0';
            rr(85) <= '1' when -129 < s else '0';
            rr(86) <= '1' when s <= -129 else '0';
            rr(87) <= '1' when -129 <= s else '0';
            rr(88) <= '1' when s > -129 else '0';
            rr(89) <= '1' when -129 > s else '0';
            rr(90) <= '1' when s >= -129 else '0';
            rr(91) <= '1' when -129 >= s else '0';
            rr(92) <= '1' when s = -129 else '0';
            rr(93) <= '1' when -129 = s else '0';
            rr(94) <= '1' when s /= -129 else '0';
            rr(95) <= '1' when -129 /= s else '0';
            rr(96) <= '1' when s < 100000 else '0';
            rr(97) <= '1' when 100000 < s else '0';
            rr(98) <= '1' when s <= 100000 else '0';
            rr(99) <= '1' when 100000 <= s else '0';
            rr(100) <= '1' when s > 100000 else '0';
            rr(101) <= '1' when 100000 > s else '0';
            rr(102) <= '1' when s >= 100000 else '0';
            rr(103) <= '1' when 100000 >= s else '0';
            rr(104) <= '1' when s = 100000 else '0';
            rr(105) <= '1' when 100000 = s else '0';
            rr(106) <= '1' when s /= 100000 else '0';
            rr(107) <= '1' when 100000 /= s else '0';
            rr(108) <= '1' when s < -100000 else '0';
            rr(109) <= '1' when -100000 < s else '0';
            rr(110) <= '1' when s <= -100000 else '0';
            rr(111) <= '1' when -100000 <= s else '0';
            rr(112) <= '1' when s > -100000 else '0';
            rr(113) <= '1' when -100000 > s else '0';
            rr(114) <= '1' when s >= -100000 else '0';
            rr(115) <= '1' when -100000 >= s else '0';
            rr(116) <= '1' when s = -100000 else '0';
            rr(117) <= '1' when -100000 = s else '0';
            rr(118) <= '1' when s /= -100000 else '0';
            rr(119) <= '1' when -100000 /= s else '0';
        end if;
    end process;
    q <= rr;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity fold_tb is end entity;
architecture test of fold_tb is
    signal clk : std_logic := '0';
    signal u   : unsigned(7 downto 0) := x"00";
    signal s   : signed(7 downto 0) := x"00";
    signal q   : std_logic_vector(119 downto 0);
begin
    dut : entity work.fold_e port map (clk, u, s, q);
    stim : process is
    begin
        for k in 0 to 11 loop
            u <= to_unsigned((k * 61 + 7) mod 256, 8);
            s <= to_signed(((k * 37 + 3) mod 256) - 128, 8);
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            report integer'image(k) & " " & to_string(q);
        end loop;
        wait;
    end process;
end architecture;
VHDL_EOF

unset NVC_ISLAND_SPEC NVC_ISLAND_SPEC_FORCE NVC_ISLAND_SPEC_STATS

nvc --std=2008 -a fold.vhd
nvc --std=2008 --rtl-islands=hier -e fold_tb
nvc --std=2008 -r fold_tb 2>&1 | grep -v '^\*\* Debug' > b.log

NVC_ISLAND_SPEC=1 NVC_ISLAND_SPEC_FORCE=A NVC_ISLAND_SPEC_STATS=1 \
   nvc --std=2008 -r fold_tb > s_raw.log 2>&1
cat s_raw.log
grep -v '^ISLAND_SPEC:' s_raw.log | grep -v '^\*\* Debug' > s.log
diff -u b.log s.log
[ "$(wc -l < b.log)" -ge 12 ]

total() {
    grep '^ISLAND_SPEC: island=' s_raw.log \
        | sed -n "s/.* $1=\([0-9]*\).*/\1/p" | awk '{ s += $1 } END { print s + 0 }'
}
[ "$(total calls_a)" -gt 0 ]
[ "$(total conv)" -eq 0 ]
