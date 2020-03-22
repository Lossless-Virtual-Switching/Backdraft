#!/usr/bin/awk -f

BEGIN {
    if(FREQ == null) {
	FREQ = 2200.0;	# In MHz
    }
    SKIPREC = 3;	# Skip first 3 records
#    SKIPLAST = 14;
    SKIPLAST = 999999;
}

/--- START ---/ {
    proc = 1;
    next;
}

/--- STOP ---/ {
    proc = 0;
    next;
}

proc == 1 {
    numrec++;
    if(numrec <= SKIPREC || numrec >= SKIPLAST) {
    	# print "Skipping:", $0;
	next;
    }

    worker = $1;
    for(i = 2; i <= NF; i++) {
	nsub = split($i, subrec, /:/);

	# Executor emits, avg. proc latency
	if(subrec[1] ~ /[0-9]/) {
	    task = subrec[1];

	    # Emitted
	    if(last_emitted[task] > subrec[2]) {
	    	print "ERROR emitted!", last_emitted[task], subrec[2];
	    }
	    emitted[task] += subrec[2] - last_emitted[task];
	    last_emitted[task] = subrec[2];
	    num_emitted[task]++;

	    # Latency
	    latency[task] += subrec[3];

	    # Processed
	    if(last_processed[task] > subrec[4]) {
		print "ERROR processed!", last_processed[task], subrec[4];
	    }
	    if(last_processed[task] != null) {
		cur_processed = subrec[4] - last_processed[task];
		print task, cur_processed;
		processed[task] += cur_processed;
		if(min_processed[task] == null || min_processed[task] > cur_processed) min_processed[task] = cur_processed;
		if(max_processed[task] == null || max_processed[task] < cur_processed) max_processed[task] = cur_processed;
		num_processed[task]++;
	    }
	    last_processed[task] = subrec[4];

	    # Tuple latency
	    tlatency[task] += subrec[5];
	}

	# Avg. tuple latency
	if(subrec[1] == "L" && subrec[2] > 0) {
	    tuple_lat[worker] += subrec[2];
	    num_tuple_lat++;
	}
    }
}

END {
    printf("######## PARAMETERS ########\n#\n# Processor frequency: %.2f GHz\n# Skipping warm-up records: %d\n#\n\n", FREQ / 1000.0, SKIPREC);
    print "# Task\tThroughput\tProc. latency [us]\tAvg/Min/Max proc. tput\tInput tuple lat [us]"
    for(task in emitted) {
	printf("%d %.2f %.2f %.2f %u %u %.2f\n", task,
	       emitted[task] / num_emitted[task],
	       (latency[task] / num_emitted[task]) / FREQ,
	       processed[task] / num_processed[task],
	       min_processed[task], max_processed[task],
	       (tlatency[task] / num_emitted[task]) / FREQ);
    }

    print "\n# Worker\tAvg. output latency [us]"
    for(worker in tuple_lat) {
	printf("%d %.2f\n", worker, (tuple_lat[worker] / num_tuple_lat) / FREQ);
    }

    print "\n# TOTAL Avg/min/max proc. tput"
    for(task in emitted) {
	sum_processed += processed[task] / num_processed[task];
	sum_min_proc += min_processed[task];
	sum_max_proc += max_processed[task];
    }
    printf("%.2f %u %u\n", sum_processed, sum_min_proc, sum_max_proc);
}
