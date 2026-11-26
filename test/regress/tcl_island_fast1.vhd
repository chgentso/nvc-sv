library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tcl_fast_dut is
    port (
        clk         : in  std_logic;
        din         : in  std_logic_vector(7 downto 0);
        obs_out     : out std_logic_vector(7 downto 0);
        pruned_comb : out std_logic_vector(7 downto 0);
        pruned_ff   : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of tcl_fast_dut is
begin
    p_comb : process (din) is
    begin
        obs_out     <= std_logic_vector(unsigned(din) + x"01");
        pruned_comb <= din xor x"5a";
    end process;

    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            pruned_ff <= std_logic_vector(unsigned(din) + x"10");
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity tcl_island_fast1 is
end entity;

architecture test of tcl_island_fast1 is
    signal clk     : std_logic := '0';
    signal din     : std_logic_vector(7 downto 0) := x"05";
    signal obs_out : std_logic_vector(7 downto 0);
begin
    dut : entity work.tcl_fast_dut
        port map (
            clk         => clk,
            din         => din,
            obs_out     => obs_out,
            pruned_comb => open,
            pruned_ff   => open
        );

    p_clk : clk <= not clk after 5 ns;

    p_stim : process is
    begin
        wait for 12 ns;
        din <= x"12";
        wait for 20 ns;
        din <= x"05";
        wait;
    end process;
end architecture;
