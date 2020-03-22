#!/usr/bin/gawk -f

function sed(regex, replace) {
    if($0 ~ regex) {
	$0 = gensub(regex, replace, "g");
	return 1;
    } else {
	return 0;
    }
}

function process(worker, ctput, clatency, tlatency) {
    if(time[worker] == null) {
    	time[worker] = 0;
    }
    tput[worker, time[worker]] = ctput;
    latency[worker, time[worker]] = clatency;
    t_latency[worker, time[worker]] = tlatency;
    time[worker]++;
}

/--- STOP ---/ {
    exit;
}

sed(".*Worker (.*) executed (.*) latency (.*), tuple latency (.*)", "\\1 \\2 \\3 \\4") ||
sed(".*Worker (.*) executed (.*) latency (.*)", "\\1 \\2 \\3 0") {
    process($1, $2, $3, $4);
}

sed("(.*) sender avglatency (.*)", "\\1 \\2") {
    worker = $1;
    slatency = $2;

    if(time[worker] == null) {
    	time[worker] = 0;
    }

    s_latency[worker, time[worker]] = slatency;
    sworkers[worker] = 1;
    time[worker]++;
}

END {
    printf("--- START ---\n");

    last_printed = 0;
    for(t = 0;; t++) {
	# Check whether to print this line
	printed = 0;
	for(w in time) {
	    if(tput[w, t] != 0) {
		printed++;
	    }
	}

	if(printed < last_printed) {
	    break;
	}
	last_printed = printed;

	printf("1 ");

	# Print it
	for(w in time) {
	    if(tput[w, t] != 0) {
		printf("%d:0:%d:%d:%d ", w, latency[w, t], tput[w, t], t_latency[w, t]);
	    }
	}

	sum = 0;
	cnt = 0;
	for(w in sworkers) {
	    # if(w == 0) continue;
	    sum += s_latency[w, t];
	    cnt++;
	}

	if(cnt > 0) {
	    printf("L:%d\n", sum / cnt);
	} else {
	    printf("\n");
	}
    }

    printf("--- STOP ---\n");
}
