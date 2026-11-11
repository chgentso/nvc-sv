-- VHDL top level of test vpi23: Verilog instances below VHDL scopes, one
-- directly in the top level and one through a VHDL entity

library ieee;
use ieee.std_logic_1164.all;

entity vpi23_mid is
    port ( clk : in  std_logic;
           d   : in  std_logic_vector(3 downto 0);
           q   : out std_logic_vector(3 downto 0) );
end entity;

architecture rtl of vpi23_mid is
    component vpi23_sub is
        port ( clk : in  std_logic;
               d   : in  std_logic_vector(3 downto 0);
               q   : out std_logic_vector(3 downto 0) );
    end component;
begin
    u_in: vpi23_sub port map ( clk => clk, d => d, q => q );
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity vpi23 is
end entity;

architecture test of vpi23 is
    component vpi23_sub is
        port ( clk : in  std_logic;
               d   : in  std_logic_vector(3 downto 0);
               q   : out std_logic_vector(3 downto 0) );
    end component;

    signal clk : std_logic := '0';
    signal d   : std_logic_vector(3 downto 0) := "0101";
    signal q1  : std_logic_vector(3 downto 0);
    signal q2  : std_logic_vector(3 downto 0);
begin
    clk <= not clk after 5 ns;

    u_sub: vpi23_sub port map ( clk => clk, d => d, q => q1 );

    u_mid: entity work.vpi23_mid port map ( clk => clk, d => d, q => q2 );

    p_end: process is
    begin
        wait for 20 ns;
        d <= "0111";
        wait for 20 ns;
        std.env.finish;
    end process;
end architecture;
