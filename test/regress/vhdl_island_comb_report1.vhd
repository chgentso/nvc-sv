library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_comb_report1_dut is
    port (
        clk   : in  std_logic;
        valid : in  std_logic;
        din   : in  unsigned(7 downto 0);
        dout  : out unsigned(7 downto 0);
        a_in  : in  unsigned(7 downto 0);
        a_out : out unsigned(7 downto 0);
        tb_o  : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of vhdl_island_comb_report1_dut is
    signal timebase : unsigned(7 downto 0) := (others => '0');
    signal tb_next  : unsigned(7 downto 0) := (others => '0');

    function helper_rep(x : unsigned(7 downto 0)) return unsigned is
    begin
        if x = x"0A" then
            report "helper comb x=" & to_hstring(x);
        end if;
        return x + 1;
    end function;
begin
    -- Clocked FF with report and assert: MUST still fuse into island
    p_ff : process(clk) is
    begin
        if rising_edge(clk) then
            timebase <= tb_next;
            assert timebase /= x"FF" report "unexpected wrap" severity failure;
            if timebase = x"02" then
                report "ff tick tb=" & to_hstring(timebase);
            end if;
        end if;
    end process;

    -- Sibling combinational process fused into island (woken every clock edge)
    p_tb_comb : process(all) is
    begin
        tb_next <= timebase + 1;
    end process;

    tb_o <= timebase;

    -- Combinational process with direct report: must NOT fire on unrelated
    -- sibling (timebase) wakeups when valid='1' and din is unchanged
    p_comb_rep : process(all) is
    begin
        dout <= din;
        if valid = '1' then
            report "comb rep din=" & to_hstring(din);
        end if;
    end process;

    -- Combinational process calling a pure function containing report + assert:
    -- must NOT fire spuriously on sibling island wakeups either
    p_comb_fn : process(all) is
    begin
        a_out <= helper_rep(a_in);
        assert a_in /= x"0A" report "comb assert a=0A" severity note;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_comb_report1 is
end entity;

architecture test of vhdl_island_comb_report1 is
    signal clk   : std_logic := '0';
    signal valid : std_logic := '0';
    signal din   : unsigned(7 downto 0) := x"00";
    signal dout  : unsigned(7 downto 0);
    signal a_in  : unsigned(7 downto 0) := x"00";
    signal a_out : unsigned(7 downto 0);
    signal tb_o  : unsigned(7 downto 0);
    signal done  : boolean := false;
begin
    dut : entity work.vhdl_island_comb_report1_dut
        port map (
            clk   => clk,
            valid => valid,
            din   => din,
            dout  => dout,
            a_in  => a_in,
            a_out => a_out,
            tb_o  => tb_o
        );

    p_clk : process is
    begin
        while not done loop
            clk <= '0';
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
        end loop;
        wait;
    end process;

    p_stim : process is
    begin
        -- Drive valid='1' and din=0x42 once, a_in=0x0A once, then hold both
        -- constant across 4 rising clock edges while timebase increments.
        wait for 2 ns;
        valid <= '1';
        din   <= x"42";
        a_in  <= x"0A";

        for i in 1 to 4 loop
            wait until rising_edge(clk);
            wait for 1 ns;
        end loop;

        assert dout = x"42" severity failure;
        assert a_out = x"0B" severity failure;
        assert tb_o = x"04" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
