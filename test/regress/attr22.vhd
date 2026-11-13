entity attr22 is
    generic (g : integer := 5);
    port (p : in bit := '1');
end entity;

architecture test of attr22 is
    attribute keep : string;
    attribute keep of p : signal is "true";
    attribute width : integer;
    attribute width of g : constant is 7;
begin
    process is
    begin
        assert p'keep = "true";
        assert g'width = 7;
        wait;
    end process;
end architecture;
