# TAS Memcached Image
`tas_memcached` is a docker file which has been setup for performing a
couple of experiments which mainly had memcached as a part of the workload application.
These setup use TAS inside a docker inorder to connect to Bess via vhost sockets.
There are some other applications included in this docker images which is used
in different experiments along with memcached.

## Old Description
TAS Memcached is used for running multiple instances of legacy test application
through Tcp-Acceleration-Service on a single machine. Each image is either a
server (memcached) or a client (mutilate).

# Files Description
The description for files found in this folder is written below:
1. build.sh:
- This script build the `tas_container` docker image. The build process needs
an archive file named post-loom.tar.xz which has some parts of the current 
repository. If the post-loom.tar.xz is not created yet this script makes it.

2. create\_postloom\_tar.sh
- This script creates the post-loom.tar.xz archive file needed for 

3. spin\_up\_tas\_container.sh
- A helpper script for creating and running a docker container
(It is a mess currently but it works)

4. boot.sh
- This script is the entry point of `tas_container`

# TODO:
1. put container building materials in a seperate directory from those helper
scripts.
2. change container to support config files instead of the mess made with
environment variables.
