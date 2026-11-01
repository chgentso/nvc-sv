library ieee; use ieee.std_logic_1164.all; use ieee.numeric_std.all;
package p is
  type t_bytes is array (natural range <>) of std_logic_vector(7 downto 0);
  constant c_CTRL : std_logic_vector(5 downto 0) := "010000";
  type t_var is record hexvalue : std_logic_vector(7 downto 0); base : unsigned(8 downto 0); bytes : t_bytes(0 to 3); end record;
  type t_vars is array (natural range <>) of t_var;
  constant c_IDX0 : integer := 0;
  constant c_IDX : integer := 1;
  constant c_VARS : t_vars(0 to 1) := (c_IDX0 => (hexvalue => x"14", base => to_unsigned(16#10#, 9), bytes => (0 => "00" & c_CTRL, 1 => x"50", others => x"ff")), c_IDX => (hexvalue => x"05", base => to_unsigned(16#20#, 9), bytes => (0 => "00" & c_CTRL, others => x"ff")));
end package;
library ieee; use ieee.std_logic_1164.all; use work.p.all;
entity casechoice1 is end;
architecture a of casechoice1 is
  signal b : std_logic_vector(7 downto 0) := x"05";
begin
  process begin
    wait for 1 ns;
    case b is
      when c_VARS(0).hexvalue => assert false report "first";
      when c_VARS(c_IDX).hexvalue => null;
      when others => assert false report "other";
    end case;
    wait;
  end process;
end;
