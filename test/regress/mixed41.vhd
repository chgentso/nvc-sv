entity mixed41_sub is
    port (en : in boolean;
          o  : out boolean);
end entity;

architecture test of mixed41_sub is
begin
    o <= not en;
end architecture;
