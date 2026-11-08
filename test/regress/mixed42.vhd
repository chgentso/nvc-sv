entity mixed42_sub is
    port (en : in boolean;
          b  : in bit;
          v  : in bit_vector(1 downto 0);
          o  : out integer);
end entity;

architecture test of mixed42_sub is
    signal primary : boolean;
    signal c1, c2, c3 : integer := 0;
begin
    primary <= true;

    p1: process is
    begin
        wait until en'event and en and primary;
        c1 <= 1;
        wait;
    end process;

    p2: process is
    begin
        wait until b'event and b = '1';
        c2 <= 10;
        wait;
    end process;

    p3: process is
    begin
        wait until v'event and v = "01";
        c3 <= 100;
        wait;
    end process;

    o <= c1 + c2 + c3;
end architecture;
