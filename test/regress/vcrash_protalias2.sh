set -x

# An alias in a protected type declaration for one of its own methods is
# accepted

cat >protalias2.vhd <<'VHD'
entity protalias2 is end;

architecture a of protalias2 is
    type counter is protected
        procedure own;
        alias own_alias is own [];
    end protected;

    type counter is protected body
        procedure own is
        begin
        end procedure;
    end protected body;
begin
end architecture;
VHD

nvc --std=19 -a protalias2.vhd
