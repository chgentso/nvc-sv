library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_mul1 is
end entity;

architecture test of vhdl_island_mul1 is
    signal clk    : std_logic := '0';
    signal sa     : signed(16 downto 0) := (others => '0');
    signal sb     : signed(16 downto 0) := (others => '0');
    signal sprod  : signed(33 downto 0) := (others => '0');
    signal ua     : unsigned(7 downto 0) := (others => '0');
    signal ub     : unsigned(11 downto 0) := (others => '0');
    signal uprod  : unsigned(19 downto 0) := (others => '0');
    signal umax   : unsigned(11 downto 0) := (others => '0');
    signal umin   : unsigned(11 downto 0) := (others => '0');
    signal umax_r : unsigned(11 downto 0) := (others => '0');
    signal umin_r : unsigned(11 downto 0) := (others => '0');
    signal done   : boolean := false;
begin
    p_comb : process (all) is
    begin
        umax <= maximum(ua, ub);
        umin <= minimum(ua, ub);
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            sprod  <= sa * sb;
            uprod  <= ua * ub;
            umax_r <= umax;
            umin_r <= umin;
        end if;
    end process;

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
        sa <= to_signed(-300, 17);
        sb <= to_signed(400, 17);
        ua <= to_unsigned(200, 8);
        ub <= to_unsigned(1000, 12);
        wait until rising_edge(clk);
        wait for 1 ns;
        assert sprod = to_signed(-120000, 34)
            report "sprod mismatch: " & integer'image(to_integer(sprod))
            severity failure;
        assert uprod = to_unsigned(200000, 20)
            report "uprod mismatch: " & integer'image(to_integer(uprod))
            severity failure;
        assert umax_r = to_unsigned(1000, 12) severity failure;
        assert umin_r = to_unsigned(200, 12) severity failure;

        sa <= to_signed(-1024, 17);
        sb <= to_signed(-512, 17);
        ua <= to_unsigned(255, 8);
        ub <= to_unsigned(100, 12);
        wait until rising_edge(clk);
        wait for 1 ns;
        assert sprod = to_signed(524288, 34) severity failure;
        assert uprod = to_unsigned(25500, 20) severity failure;
        assert umax_r = to_unsigned(255, 12) severity failure;
        assert umin_r = to_unsigned(100, 12) severity failure;

        done <= true;
        wait;
    end process;
end architecture;
