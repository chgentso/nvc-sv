entity nested_pkg_body1 is
    package p is
        function f return integer;
    end package;
end entity;

architecture test of nested_pkg_body1 is
begin
end architecture;
