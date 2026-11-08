entity mixed1 is
end entity;

library ieee;
use ieee.std_logic_1164.all;

architecture test of mixed1 is
    signal x, y, z : std_logic;
begin

    b1: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one, two : in std_logic;
                   three : out std_logic );
        end component;
    begin
        u: component mod1
            port map ( x, y, z );       -- OK
    end block;

    b2: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in std_logic;
                   three : out std_logic );
        end component;
    begin
        u: component mod1
            port map ( x, z );       -- Error
    end block;

    b3: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one, two : in std_logic;
                   three, four : out std_logic );
        end component;
    begin
        u: component mod1
            port map ( x, y, z, z );       -- Error
    end block;

    b4: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one, two : in std_logic;
                   three : out bit );
        end component;
    begin
        u: component mod1
            port map ( x, y, open );       -- Error
    end block;

    b5: block is
        component mod1 is
            port ( one, two : in std_logic;
                   three : out std_logic );
        end component;
    begin
        u: component mod1
            port map ( x, y, z );       -- Error
    end block;

    b6: block is
        component mod1 is
            generic ( g1 : std_logic );
            port ( one, two : in std_logic;
                   three : out std_logic );
        end component;
    begin
        u: component mod1
            generic map ( g1 => '1' )
            port map ( x, y, z );       -- Error
    end block;

    b7: block is
        component mod1 is
            generic ( g1, g2 : integer );
            port ( one, two : in std_logic;
                   three : out std_logic );
        end component;
    begin
        u: component mod1
            generic map ( 5, 6 )
            port map ( x, y, z );       -- Error
    end block;

    b8: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one, two : in std_logic;
                   three : in std_logic );
        end component;
    begin
        u: component mod1
            port map ( x, y, z );       -- Error
    end block;

    b9: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in std_logic;
                   two : in std_logic;
                   three : out bit_vector(0 to 0) );
        end component;
        signal s : bit_vector(0 to 0);
    begin
        u: component mod1
            port map ( x, y, s );       -- Error
    end block;

    b10: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in bit_vector(0 to 0);
                   two : in std_logic;
                   three : out std_logic );
        end component;
        signal s : bit_vector(0 to 0);
    begin
        u: component mod1
            port map ( s, y, z );       -- Error
    end block;

    b11: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in std_logic;
                   two : in std_logic;
                   three : out integer );
        end component;
        signal s : integer;
    begin
        u: component mod1
            port map ( x, y, s );       -- Error
    end block;

    b12: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in integer;
                   two : in std_logic;
                   three : out std_logic );
        end component;
        signal s : integer;
    begin
        u: component mod1
            port map ( s, y, z );       -- Error
    end block;

    b13: block is
        component mod1 is
            generic ( g1 : integer := 0 );
            port ( one : in bit;
                   two : in std_logic;
                   three : out std_logic );
        end component;
        signal s : bit;
    begin
        u: component mod1
            port map ( s, y, z );       -- Error
    end block;

end architecture;
