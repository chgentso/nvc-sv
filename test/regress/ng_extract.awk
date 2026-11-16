# Generics of the instances in a --netlist file, one line per generic: the
# cell type, the name, type, kind and value, and whether it was overridden
/^"attributes":/ && cand { cell = "" }
/^"[^"]+":\{$/ { cand = 1; next }
/^"type":/ && cand { cell = $0; sub(/^"type":/, "", cell); sub(/,$/, "", cell) }
{ cand = 0 }
/^"generics":\[/ { ing = 1; next }
ing && /^\]/ { ing = 0; next }
ing && /^"name":/ { n = $0; sub(/^"name":/, "", n); sub(/,$/, "", n); rec = cell " " n; next }
ing && /^"(type|kind|value)":/ { v = $0; sub(/,$/, "", v); sub(/^"[a-z]+":/, "", v); rec = rec " " v; next }
ing && /^"overridden":/ { v = $0; sub(/[},]+$/, "", v); sub(/^"overridden":/, "", v);
                          if (cell != "") print rec " " v; next }
ing && /^"exact":/ { print "   not exact"; next }
