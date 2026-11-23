library ieee;
use ieee.std_logic_1164.all;

entity missingunit is
end entity;

architecture test of missingunit is
    signal pcie_rxn : std_logic;
    signal vec      : std_logic_vector(3 downto 0);
begin

    u1: entity work.not_here
        generic map ( pcie_rxn => "00" )
        port map ( pcie_rxn => X"00", vec => "0101", vec(1) => "1" );  -- OK

    u2: entity work.not_here(rtl)
        port map ( pcie_rxn, "0101", (others => '0'), vec(3 downto 2) => "11" );  -- OK

    u3: nocomp port map ( pcie_rxn => X"00", vec => (others => '1') );  -- Error
end architecture;
