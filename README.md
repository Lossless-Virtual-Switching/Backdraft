# Backdraft
Backdraft is a new lossless virtual switch that addresses the slow receiver problem by combining three new components: (1) Dynamic Per-Flow Queuing (DPFQ) to prevent HOL blocking and provide on-demand memory usage; (2) Doorbell queues to reduce CPU overheads; (3) A new overlay network to avoid congestion spreading. Our NSDI22 Paper describes the Backdraft design and its rationale.

[[Paper]](https://www.usenix.org/system/files/nsdi22-paper-sanaee.pdf)
[[Slide]](https://www.usenix.org/system/files/nsdi22_slides_sanaee.pdf)

## What is the slow receiver problem?
Any networked application is potentially a slow receiver if it is unable to
keep up with the incoming rate.

## System Architetcure
![Backdraft](docs/pngs/bd_design.png)

## Repo Layout
```
- /
 - env: all scripts regarding environment setup
 - exp: all scripts regarding experiments in the paper
 - code: all the codes and libraries built for Backdraft
  - tas: tas library for the userspace TCP/DCTCP
  - Homa: Homa openloop app workload generator
  - apps: dummy applications to generate non-cooperative workload.
  - dpdk: DPDK
  - lib: backdraft interface library
  - tas-benchmark: tas applications
```

For installing Backdraft look [here](./INSTALL.md) and for some
details about experiments look [here](./exp/notes/README.md).

## License

MIT

## Disclaimer

This is a research prototype.

## Help

Please use [Github Issues](https://github.com/Lossless-Virtual-Switching/Backdraft/issues).

>>> Still working on the readme!
