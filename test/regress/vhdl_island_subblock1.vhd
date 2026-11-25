entity subblock_dut is
    generic (
        ASYNC_OUT : boolean := false
    );
    port (
        clk     : in  bit;
        rst_in  : in  bit;
        rst_out : out bit
    );
end entity;

architecture rtl of subblock_dut is
    signal chain : bit_vector(2 downto 0) := "111";
    signal sync  : bit_vector(1 downto 0) := "11";
begin
    p_rst : process (rst_in, clk) is
    begin
        if rst_in = '1' then
            chain <= "111";
        elsif clk'event and clk = '1' then
            chain <= chain(1 downto 0) & '0';
        end if;
    end process;

    g_sync : if not ASYNC_OUT generate
        p_sync : process (clk) is
        begin
            if clk'event and clk = '1' then
                sync <= sync(0 downto 0) & chain(2);
            end if;
        end process;
        rst_out <= sync(1);
    end generate;
end architecture;

entity vhdl_island_subblock1 is
end entity;

architecture test of vhdl_island_subblock1 is
    signal clk     : bit := '0';
    signal rst_in  : bit := '0';
    signal rst_out : bit;
begin
    dut : entity work.subblock_dut
        generic map (ASYNC_OUT => false)
        port map (clk => clk, rst_in => rst_in, rst_out => rst_out);

    stim : process is
    begin
        wait for 5 ns;
        assert rst_out = '1' report "expected initial rst_out = 1";

        for i in 1 to 6 loop
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end loop;

        assert rst_out = '0' report "expected rst_out to deassert after shift";

        -- Re-assert asynchronously and verify it clears again
        rst_in <= '1';
        wait for 2 ns;
        rst_in <= '0';
        for i in 1 to 2 loop
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end loop;
        assert rst_out = '1' report "expected rst_out = 1 after async pulse";

        for i in 1 to 6 loop
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
            wait for 5 ns;
        end loop;
        assert rst_out = '0' report "expected rst_out = 0 after second shift";
        wait;
    end process;
end architecture;
