-- 'PATH_NAME and 'INSTANCE_NAME for loop parameters, loop labels, and
-- VHDL-2019 sequential block statements (LRM 16.2.5).

entity attr31 is
end entity;

architecture test of attr31 is
begin
    main: process is
        variable top_v : integer := 0;
    begin
        l1: for i in 1 to 1 loop
            assert i'path_name = ":attr31:main:l1:i";
            assert i'instance_name = ":attr31(test):main:l1:i";
            assert l1'path_name = ":attr31:main:l1:";
            assert top_v'path_name = ":attr31:main:top_v";

            l2: for j in 1 to 1 loop
                assert j'path_name = ":attr31:main:l1:l2:j";
                assert l2'path_name = ":attr31:main:l1:l2:";
                assert i'path_name = ":attr31:main:l1:i";
            end loop;
        end loop;

        for k in 1 to 1 loop
            assert k'path_name = ":attr31:main::k";
        end loop;

        sb: block is
            variable q : integer := 1;
        begin
            assert q'path_name = ":attr31:main:sb:q";
            assert q'instance_name = ":attr31(test):main:sb:q";
            assert sb'path_name = ":attr31:main:sb:";
        end block;

        block is
            variable r : integer := 2;
        begin
            assert r'path_name = ":attr31:main::r";
        end block;

        wait;
    end process;
end architecture;
