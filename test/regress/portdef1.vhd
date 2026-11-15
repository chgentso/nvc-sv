library ieee;
use ieee.std_logic_1164.all;

entity sub is
    port ( a : in std_logic_vector := (others => '0');
           b : in bit_vector := (others => '1');
           ones : out integer;
           len  : out integer );
end entity;

architecture t of sub is
begin
    process (a, b) is
        variable n : integer := 0;
    begin
        n := 0;
        for i in b'range loop
            if b(i) = '1' then n := n + 1; end if;
        end loop;
        ones <= n;
        len <= a'length;
    end process;
end architecture;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;

entity portdef1 is
end entity;

architecture test of portdef1 is
    signal s : std_logic_vector(3 downto 0) := "1010";
    signal o1, o2, l1, l2, o3, l3 : integer;
    signal empty : std_logic_vector(1 to 0);
begin

    u1: entity work.sub
        port map ( a => s, b => "101", ones => o1, len => l1 );

    u2: entity work.sub
        port map ( a => "11", b => (1 to 5 => '1'), ones => o2, len => l2 );

    u3: entity work.sub
        port map ( a => empty, b => "", ones => o3, len => l3 );

    check: process is
    begin
        wait for 1 ns;
        assert o1 = 2 and l1 = 4;
        assert o2 = 5 and l2 = 2;
        assert o3 = 0 and l3 = 0;
        wait;
    end process;

end architecture;
