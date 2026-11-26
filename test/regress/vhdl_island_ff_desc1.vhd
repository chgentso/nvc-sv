library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Exercises compact 16-byte FF descriptors across odd/even small & medium
-- widths (1, 2, 3, 5, 7, 12, 16, 24, 32, 48, 64, 72 bits) plus dynamic
-- descriptor refresh on unprune, force/release, and direct-link break.
entity ff_desc_src is
    port (
        clk   : in  std_logic;
        seed  : in  unsigned(7 downto 0);
        w1    : out std_logic;
        w2    : out unsigned(1 downto 0);
        w3    : out unsigned(2 downto 0);
        w5    : out unsigned(4 downto 0);
        w7    : out unsigned(6 downto 0);
        w12   : out unsigned(11 downto 0);
        w16   : out unsigned(15 downto 0);
        w24   : out unsigned(23 downto 0);
        w32   : out unsigned(31 downto 0);
        w48   : out unsigned(47 downto 0);
        w64   : out unsigned(63 downto 0);
        w72   : out unsigned(71 downto 0);
        pruned_w5  : out unsigned(4 downto 0);
        pruned_w24 : out unsigned(23 downto 0)
    );
end entity;

architecture rtl of ff_desc_src is
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            w1  <= seed(0);
            w2  <= seed(1 downto 0) + "01";
            w3  <= seed(2 downto 0) + "011";
            w5  <= seed(4 downto 0) + "00101";
            w7  <= seed(6 downto 0) + "0000111";
            w12 <= (seed & seed(3 downto 0)) + x"012";
            w16 <= (seed & not seed) + x"0016";
            w24 <= (seed & seed & not seed) + x"000024";
            w32 <= (seed & not seed & seed & not seed) + x"00000032";
            w48 <= (seed & seed & not seed & seed & not seed & seed) + x"000000000048";
            w64 <= (seed & not seed & seed & not seed & seed & not seed & seed & not seed) + x"0000000000000064";
            w72 <= (seed & seed & not seed & seed & not seed & seed & not seed & seed & not seed) + x"000000000000000072";
            pruned_w5  <= seed(4 downto 0) xor "10101";
            pruned_w24 <= (seed & seed & seed) xor x"a55a3c";
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ff_desc_sink is
    port (
        w1   : in  std_logic;
        w2   : in  unsigned(1 downto 0);
        w3   : in  unsigned(2 downto 0);
        w5   : in  unsigned(4 downto 0);
        w7   : in  unsigned(6 downto 0);
        w12  : in  unsigned(11 downto 0);
        w16  : in  unsigned(15 downto 0);
        w24  : in  unsigned(23 downto 0);
        w32  : in  unsigned(31 downto 0);
        w48  : in  unsigned(47 downto 0);
        w64  : in  unsigned(63 downto 0);
        w72  : in  unsigned(71 downto 0);
        chk  : out unsigned(31 downto 0)
    );
end entity;

architecture rtl of ff_desc_sink is
begin
    p_comb : process (all) is
        variable acc : unsigned(31 downto 0);
    begin
        acc := w32 xor (w24 & w7 & w1);
        acc := acc xor (w16 & w12 & w2 & w2);
        acc := acc xor (w48(31 downto 0) xor resize(w48(47 downto 32), 32));
        acc := acc xor (w64(31 downto 0) xor w64(63 downto 32));
        acc := acc xor (w72(31 downto 0) xor w72(63 downto 32) xor resize(w72(71 downto 64), 32));
        acc := acc xor resize(w5 & w3, 32);
        chk <= acc;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_ff_desc1 is
end entity;

architecture test of vhdl_island_ff_desc1 is
    signal clk  : std_logic := '0';
    signal seed : unsigned(7 downto 0) := x"13";
    signal w1   : std_logic;
    signal w2   : unsigned(1 downto 0);
    signal w3   : unsigned(2 downto 0);
    signal w5   : unsigned(4 downto 0);
    signal w7   : unsigned(6 downto 0);
    signal w12  : unsigned(11 downto 0);
    signal w16  : unsigned(15 downto 0);
    signal w24  : unsigned(23 downto 0);
    signal w32  : unsigned(31 downto 0);
    signal w48  : unsigned(47 downto 0);
    signal w64  : unsigned(63 downto 0);
    signal w72  : unsigned(71 downto 0);
    signal chk  : unsigned(31 downto 0);
    signal go_dyn : boolean := false;
    signal done   : boolean := false;

    function expected_chk(s : unsigned(7 downto 0)) return unsigned is
        variable v1  : std_logic := s(0);
        variable v2  : unsigned(1 downto 0) := s(1 downto 0) + "01";
        variable v3  : unsigned(2 downto 0) := s(2 downto 0) + "011";
        variable v5  : unsigned(4 downto 0) := s(4 downto 0) + "00101";
        variable v7  : unsigned(6 downto 0) := s(6 downto 0) + "0000111";
        variable v12 : unsigned(11 downto 0) := (s & s(3 downto 0)) + x"012";
        variable v16 : unsigned(15 downto 0) := (s & not s) + x"0016";
        variable v24 : unsigned(23 downto 0) := (s & s & not s) + x"000024";
        variable v32 : unsigned(31 downto 0) := (s & not s & s & not s) + x"00000032";
        variable v48 : unsigned(47 downto 0) := (s & s & not s & s & not s & s) + x"000000000048";
        variable v64 : unsigned(63 downto 0) := (s & not s & s & not s & s & not s & s & not s) + x"0000000000000064";
        variable v72 : unsigned(71 downto 0) := (s & s & not s & s & not s & s & not s & s & not s) + x"000000000000000072";
        variable acc : unsigned(31 downto 0);
    begin
        acc := v32 xor (v24 & v7 & v1);
        acc := acc xor (v16 & v12 & v2 & v2);
        acc := acc xor (v48(31 downto 0) xor resize(v48(47 downto 32), 32));
        acc := acc xor (v64(31 downto 0) xor v64(63 downto 32));
        acc := acc xor (v72(31 downto 0) xor v72(63 downto 32) xor resize(v72(71 downto 64), 32));
        acc := acc xor resize(v5 & v3, 32);
        return acc;
    end function;
begin
    u_src : entity work.ff_desc_src
        port map (
            clk => clk, seed => seed,
            w1 => w1, w2 => w2, w3 => w3, w5 => w5, w7 => w7,
            w12 => w12, w16 => w16, w24 => w24, w32 => w32,
            w48 => w48, w64 => w64, w72 => w72,
            pruned_w5 => open, pruned_w24 => open
        );

    u_sink : entity work.ff_desc_sink
        port map (
            w1 => w1, w2 => w2, w3 => w3, w5 => w5, w7 => w7,
            w12 => w12, w16 => w16, w24 => w24, w32 => w32,
            w48 => w48, w64 => w64, w72 => w72,
            chk => chk
        );

    -- Dynamically unprune pruned_w5/pruned_w24 and break direct link on w5/w24
    p_dyn : process is
    begin
        wait until go_dyn;

        assert << signal u_src.pruned_w5 : unsigned(4 downto 0) >> = ("10011" xor "10101")
            report "initial pruned_w5 mismatch";
        assert << signal u_src.pruned_w24 : unsigned(23 downto 0) >> = ((x"13" & x"13" & x"13") xor x"a55a3c")
            report "initial pruned_w24 mismatch";

        -- Force pruned_w5 and direct-linked w5 (breaks direct link dynamically!)
        << signal u_src.pruned_w5 : unsigned(4 downto 0) >> <= force "11011";
        << signal u_src.w5        : unsigned(4 downto 0) >> <= force "10001";
        wait for 1 ns;
        assert << signal u_src.pruned_w5 : unsigned(4 downto 0) >> = "11011"
            report "forced pruned_w5 mismatch";
        assert << signal u_src.w5 : unsigned(4 downto 0) >> = "10001"
            report "forced w5 mismatch";

        wait for 14 ns;
        assert << signal u_src.pruned_w5 : unsigned(4 downto 0) >> = "11011"
            report "pruned_w5 did not hold force";
        assert << signal u_src.w5 : unsigned(4 downto 0) >> = "10001"
            report "w5 did not hold force";

        -- Release both: must restore driven values for seed=0x5a (low 5 bits = 11010)
        << signal u_src.pruned_w5 : unsigned(4 downto 0) >> <= release;
        << signal u_src.w5        : unsigned(4 downto 0) >> <= release;
        wait for 1 ns;
        assert << signal u_src.pruned_w5 : unsigned(4 downto 0) >> = ("11010" xor "10101")
            report "released pruned_w5 mismatch";
        assert << signal u_src.w5 : unsigned(4 downto 0) >> = ("11010" + "00101")
            report "released w5 mismatch";

        -- Wait for next clock edge with seed=0xc7 (low 5 bits = 00111; verifies broken direct link still updates sink!)
        wait for 14 ns;
        assert << signal u_src.pruned_w5 : unsigned(4 downto 0) >> = ("00111" xor "10101")
            report "post-release pruned_w5 mismatch";
        assert << signal u_src.pruned_w24 : unsigned(23 downto 0) >> = ((x"c7" & x"c7" & x"c7") xor x"a55a3c")
            report "post-release pruned_w24 mismatch";
        done <= true;
        wait;
    end process;

    p_stim : process is
    begin
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;
        assert chk = expected_chk(x"13") report "cycle 1 chk mismatch";

        -- Trigger dynamic unprune + direct-link break + force
        go_dyn <= true;
        wait for 5 ns;

        -- Cycle 2 with seed=0x5a while w5 is forced
        seed <= x"5a";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';

        -- Wait for release, then cycle 3 with seed=0xc7
        wait for 5 ns;
        seed <= x"c7";
        wait for 5 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;
        assert chk = expected_chk(x"c7") report "cycle 3 chk mismatch after direct-link break";

        -- Cycle 4 unchanged seed=0xc7 (tests no-change path across all widths)
        wait for 4 ns;
        clk <= '1'; wait for 5 ns; clk <= '0';
        wait for 1 ns;
        assert chk = expected_chk(x"c7") report "cycle 4 chk mismatch";

        wait until done;
        wait;
    end process;
end architecture;
