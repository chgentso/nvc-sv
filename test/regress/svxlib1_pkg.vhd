library ieee; use ieee.std_logic_1164.all;
package svxlib1_vcomp is
  component svxlib1_vm generic (W : integer := 4); port (clk : in std_logic; d : in std_logic_vector(W-1 downto 0); q : out std_logic_vector(W-1 downto 0)); end component;
end package;
