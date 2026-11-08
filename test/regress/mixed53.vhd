library ieee; use ieee.std_logic_1164.all;
package mixed53_pkg is
  type rec_t is record
    valid : std_logic;
    mask  : std_logic_vector(3 downto 0);
  end record;
end package;
library ieee; use ieee.std_logic_1164.all; use work.mixed53_pkg.all;
use ieee.numeric_std.all;
entity mixed53_leaf is generic (id : integer); port (rp : out rec_t); end;
architecture a of mixed53_leaf is
  signal loc : rec_t := ('1', "0001");
begin
  rp.valid <= '1';
  rp.mask <= std_logic_vector(to_unsigned(id, 4));
end;
library ieee; use ieee.std_logic_1164.all; use work.mixed53_pkg.all;
entity mixed53_e is end;
architecture a of mixed53_e is
  type rec_arr is array (0 to 1) of rec_t;
  signal outs : rec_arr;
begin
  g: for i in 0 to 1 generate
    signal gs : rec_t := ('0', "1010");
  begin
    l: entity work.mixed53_leaf generic map (id => i + 3) port map (rp => outs(i));
  end generate;
end;
