library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_fallback1 is
end entity;

architecture test of vhdl_island_fallback1 is
    signal clk     : std_logic := '0';
    signal sel     : integer range 2 to 3 := 2;
    signal din     : unsigned(7 downto 0) := (others => '0');
    signal good_q  : unsigned(7 downto 0) := (others => '0');
    signal good_c  : unsigned(7 downto 0) := (others => '0');
    signal bad_bit : std_logic := '0';
    signal done    : boolean := false;
begin
    -- Good FF process (shares clk and communicates with both good_comb and bad_ff)
    p_good_ff : process (clk) is
    begin
        if rising_edge(clk) then
            good_q <= din + 1;
        end if;
    end process;

    -- Good comb process reading good_q (from island) and bad_bit (from ejected proc)
    p_good_comb : process (all) is
    begin
        if bad_bit = '1' then
            good_c <= good_q + 10;
        else
            good_c <= good_q;
        end if;
    end process;

    -- Process that passes island analysis (ATTR_VAL is accepted) but falls back
    -- in MIR lowering; only this process should be ejected while p_good_ff and
    -- p_good_comb stay fused in the island.
    p_bad_ff : process (clk) is
    begin
        if rising_edge(clk) then
            if good_q(0) = '1' then
                bad_bit <= std_logic'val(sel);
            else
                bad_bit <= '0';
            end if;
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
        din <= to_unsigned(4, 8);  -- good_q becomes 5 (odd)
        sel <= 3;                  -- std_logic'val(3) = '1'
        wait until rising_edge(clk);
        wait for 1 ns;
        assert good_q = to_unsigned(5, 8) severity failure;
        assert bad_bit = '0' severity failure;
        assert good_c = to_unsigned(5, 8) severity failure;

        -- Next edge: p_bad_ff sees good_q(0) = '1' and sets bad_bit <= '1'
        din <= to_unsigned(6, 8);  -- good_q becomes 7
        wait until rising_edge(clk);
        wait for 1 ns;
        assert good_q = to_unsigned(7, 8) severity failure;
        assert bad_bit = '1' severity failure;
        assert good_c = to_unsigned(17, 8) severity failure;

        sel <= 2;                  -- std_logic'val(2) = '0'
        din <= to_unsigned(10, 8); -- good_q becomes 11
        wait until rising_edge(clk);
        wait for 1 ns;
        assert good_q = to_unsigned(11, 8) severity failure;
        assert bad_bit = '0' severity failure;
        assert good_c = to_unsigned(11, 8) severity failure;

        done <= true;
        wait;
    end process;
end architecture;
