# Building Backdraft

The scripts for building Backdraft is available at `/env/ansible`.
If you are using a Mellanox network interface card then first run
`install_ofed.sh` (or install OFED manaully). Dependancies can be installed
using `install.sh`. Finally some major sub-programs in this repository
(includeing the virtual switch) can be built using `build_repo.sh`.


This scripts were tested on `Ubunutu 20.04` and
`Mellanox Technologies MT28800 Family [ConnectX-5 Ex]`.
