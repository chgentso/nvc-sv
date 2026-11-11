library ieee;
use ieee.std_logic_1164.all;

-- Waveform scopes of a Verilog module bound to a component
entity wrapnames4 is
end entity;

architecture test of wrapnames4 is
    component wn4_v is
        port ( a : in std_logic_vector(1 downto 0);
               b : out std_logic_vector(1 downto 0) );
    end component;

    signal a : std_logic_vector(1 downto 0) := "01";
    signal b : std_logic_vector(1 downto 0);
begin
    u_v : component wn4_v port map ( a => a, b => b );

    a <= "10" after 1 ns;
end architecture;
