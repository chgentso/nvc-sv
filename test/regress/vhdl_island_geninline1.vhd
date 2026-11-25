library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_geninline1 is
    generic (
        ENABLE_LANES : boolean := true
    );
end entity;

architecture test of vhdl_island_geninline1 is
    type u8_arr_t is array (0 to 3) of unsigned(7 downto 0);

    signal clk      : std_logic := '0';
    signal rst      : std_logic := '1';
    signal step     : unsigned(7 downto 0) := (others => '0');
    signal lanes    : u8_arr_t := (others => (others => '0'));
    signal lane_sum : unsigned(7 downto 0) := (others => '0');
    signal done     : boolean := false;
begin
    -- Parent FF process driving step
    p_step : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                step <= (others => '0');
            else
                step <= step + 1;
            end if;
        end if;
    end process;

    -- Nested if-generate + for-generate inlined into parent architecture island
    g_en : if ENABLE_LANES generate
        g_lanes : for i in 0 to 3 generate
            p_lane : process (clk) is
            begin
                if rising_edge(clk) then
                    if rst = '1' then
                        lanes(i) <= to_unsigned(i * 16, 8);
                    else
                        lanes(i) <= lanes(i) + step + to_unsigned(i, 8);
                        if i = 2 and step = x"02" then
                            report "gen lane2=" & to_hstring(lanes(i));
                        end if;
                    end if;
                end if;
            end process;
        end generate;
    end generate;

    -- Untaken if-generate must not interfere with parent signals
    g_dis : if not ENABLE_LANES generate
        p_unused : process (clk) is
        begin
            if rising_edge(clk) then
                lanes(0) <= x"ff";
            end if;
        end process;
    end generate;

    p_sum : process (all) is
    begin
        lane_sum <= lanes(0) + lanes(1) + lanes(2) + lanes(3);
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

    p_check : process is
    begin
        wait until rising_edge(clk);
        wait for 1 ns;
        rst <= '0';
        assert lanes(0) = x"00" and lanes(1) = x"10"
           and lanes(2) = x"20" and lanes(3) = x"30" severity failure;
        assert lane_sum = x"60" severity failure;

        -- Cycle 1: step was 0 -> lanes(i) += 0 + i
        wait until rising_edge(clk);
        wait for 1 ns;
        assert lanes(0) = x"00" and lanes(1) = x"11"
           and lanes(2) = x"22" and lanes(3) = x"33" severity failure;

        -- Cycle 2: step was 1 -> lanes(i) += 1 + i
        wait until rising_edge(clk);
        wait for 1 ns;
        assert lanes(0) = x"01" and lanes(1) = x"13"
           and lanes(2) = x"25" and lanes(3) = x"37" severity failure;

        -- Cycle 3: step was 2 -> report fires with lane2=25, lanes(i) += 2 + i
        wait until rising_edge(clk);
        wait for 1 ns;
        assert lanes(0) = x"03" and lanes(1) = x"16"
           and lanes(2) = x"29" and lanes(3) = x"3c" severity failure;

        done <= true;
        wait;
    end process;
end architecture;
