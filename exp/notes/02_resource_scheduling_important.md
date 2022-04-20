# Resource scheduling is important

## Setting up the environment

Use script `/exp/motivation/variable_throughput/run_memcached_exp.sh`
for this experiment.

Edit the script and set `EXPERIMENT` to 
`$FIXED_VALUE_NO_BG` for experiment with no background work or to
`$FIXED_VALUE_BG` for when there is a background program.

Update `server_ip=node2` and set `agent_machines=(node3 node4 node5)`.

## Running the experiment

> the script is at `/exp/motivation/variable_throughput/`

```
bash run_memcached_exp.sh
```

## Immediate results

The results of mutilate is printed to the `stdout`.
Timestamp of beginning and end of each request along its size is also recorded.
The results will be in `/tmp/mem_exp/` there will be a `.txt` file for
each workload node.

The timestamp files will be used to calculate throughput in fine granularity.

> If the results of the previous experiment is not moved to another place,
the script prevents running a new experiment.

## Calculate throughput

`analysis.py` script can be used to calculate throughput in predefined
time-windows.

The script needs `intervaltree` module that can be installed as follows.

```
sudo apt install python3-pip
pip3 install intervaltree numpy
```

Script can be used as defined below.

```
usage: analysis.py [-h] [--tick-interval TICK_INTERVAL] [--parallel PARALLEL] wnd_size trace_type dir_res trace_files [trace_files ...]
```

* We used window size of `30`.
* Trace type is `MEMCACHED`.
* Define a proper directory for the results.
* Pass the timestamp files to the script.

## Figure

The figure is the CDF of throughput which was calculated in previous stage.

## TODO

* BG1 script is not on the repo?!

