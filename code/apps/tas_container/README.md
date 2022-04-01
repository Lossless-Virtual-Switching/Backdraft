# TAS Container
TAS Container is used for running multiple instances of legacy test application
through TCP-Acceleration-Service on a single machine.

# Files Description
The description for files found in this folder is written below:
1. build.sh:
- This script builds the `tas_container` docker image. The build process needs
an archive file named post-loom.tar.xz which has some parts of the current 
repository. If the post-loom.tar.xz is not created yet this script makes it.

2. create\_postloom\_tar.sh
- This script creates the post-loom.tar.xz archive file.

3. spin\_up\_tas\_container.sh
- A helper script for creating and running a docker container.

4. boot.sh
- This script is the entry point of `tas_container`

# TODO:
1. put container building materials in a seperate directory from those helper
scripts.
2. change container to support config files instead of the mess made with
environment variables.
