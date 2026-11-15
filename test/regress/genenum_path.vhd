entity genenum_path is
end entity;

architecture test of genenum_path is
    type color is (Red, Green, Blue);
begin
    g: for c in Green to Blue generate
        signal gs : bit;
    begin
        process is
        begin
            if c = Blue then
                assert gs'path_name = ":genenum_path:g(blue):gs"
                    report gs'path_name severity failure;
            end if;
            wait;
        end process;
    end generate;

    h: for ch in character range 'x' to 'y' generate
        constant k : integer := 1;
    begin
        process is
        begin
            if ch = 'y' then
                assert k'path_name = ":genenum_path:h('y'):k"
                    report k'path_name severity failure;
            end if;
            wait;
        end process;
    end generate;
end architecture;
