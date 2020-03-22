#!/usr/bin/gawk -f

$0 ~ /#else/  || $0 ~ /#elif/ && prt == 1 {
    exit;
}

$0 ~ "defined\\(" TOPOLOGY "\\)" {
    prt = 1;
}

prt == 1 && /\.hostname = \"/ && $0 !~ /\/\*/ {
    if(DPDK == 1) {
	a = gensub(/.*\.hostname = "([^"]*)".*/, "\\1", "g");
    } else {
	a = gensub(/.*\/\/(.*)/, "\\1", "g");
    }
    print a;
}

END {
    if(prt != 1) {
	printf("%s: not a valid topology\n", TOPOLOGY) > "/dev/stderr";
	exit 1;
    }
}
