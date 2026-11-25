library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_comb_edge1_dut is
    port (
        clk    : in  std_logic;
        rst    : in  std_logic;
        sclk_o : out std_logic;
        mosi_o : out std_logic
    );
end entity;

architecture rtl of vhdl_island_comb_edge1_dut is
    type reg_r is record
        sclk : std_logic;
        mosi : std_logic;
    end record;
    signal r : reg_r := (sclk => '0', mosi => '0');
begin
    p_seq : process (clk) is
    begin
        if rising_edge(clk) then
            if rst = '1' then
                r.sclk <= '0';
                r.mosi <= '0';
            else
                r.sclk <= not r.sclk;
                if r.sclk = '1' then
                    r.mosi <= not r.mosi;
                end if;
            end if;
        end if;
    end process;

    sclk_o <= r.sclk;
    mosi_o <= r.mosi;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vhdl_island_comb_edge1 is
end entity;

architecture test of vhdl_island_comb_edge1 is
    signal clk  : std_logic := '0';
    signal rst  : std_logic := '1';
    signal sclk : std_logic;
    signal mosi : std_logic;
    signal done : boolean := false;
begin
    clk <= not clk after 5 ns when not done else '0';

    i_dut : entity work.vhdl_island_comb_edge1_dut
        port map (
            clk    => clk,
            rst    => rst,
            sclk_o => sclk,
            mosi_o => mosi
        );

    p_stim : process is
    begin
        wait for 20 ns;
        rst <= '0';
        wait for 500 ns;
        assert done report "timeout waiting for sclk edges" severity failure;
        wait;
    end process;

    p_mon : process is
        variable rises : natural := 0;
        variable falls : natural := 0;
    begin
        wait until rst = '0';
        for i in 1 to 4 loop
            wait until rising_edge(sclk);
            assert sclk = '1' and sclk'event severity failure;
            rises := rises + 1;
            wait until falling_edge(sclk);
            assert sclk = '0' and sclk'event severity failure;
            falls := falls + 1;
        end loop;
        assert rises = 4 and falls = 4 severity failure;
        done <= true;
        wait;
    end process;
end architecture;
