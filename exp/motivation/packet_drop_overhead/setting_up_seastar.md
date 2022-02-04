# Setting Up Seastar

## Acquiring Source and Building

1. `git clone https://github.com/scylladb/seastar`
1. `git checkout 1d5c9831bb0ab84fb700f92f48378f72267b5c90`
1. `git submodule update --init --recursive`
1. `./configure.py --enable-dpdk --mode=release --without-tests --without-demo`
1. `cd build/release`
1. `ninja`

## Running memcached

```sh
sudo ./memcached \
    --port 11211 \
    --dhcp 0 \
    --host-ipv4-addr 192.168.1.2 \
    --netmask-ipv4-addr 255.255.255.0 \
    --network-stack native \
    -m 8G \
    --dpdk-pmd \
    --smp 4
```

> may need to increase `aio-max-nr`. The error message is clear.
> `sudo sysctl -w fs.aio-max-nr=999999999`

## Configuring to Connect a Vitrio Device

First, notice that `librte_pmd_virtio` is not linked with project
binaries and libraries. First should change `CMakelist` files to
link with this driver. Edit these files.

* cmake/Finddpdk.cmake
* apps/CMakelists.txt

```
# Finddpdk.cmake
find_library (dpdk_PMD_VIRTIO_LIBRARY rte_pmd_virtio)

set (dpdk_REQUIRED
  dpdk_INCLUDE_DIR
  dpdk_PMD_VMXNET3_UIO_LIBRARY
  dpdk_PMD_I40E_LIBRARY
  dpdk_PMD_VIRTIO_LIBRARY
  ...

set (dpdk_LIBRARIES
    ...
    ${dpdk_PMD_VIRTIO_LIBRARY}
    ...

#
# pmd_virtio
#

add_library (dpdk::pmd_virtio UNKNOWN IMPORTED)

set_target_properties (dpdk::pmd_virtio
    PROPERTIES
    IMPORTED_LOCATION ${dpdk_PMD_VIRTIO_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${dpdk_INCLUDE_DIR})

set (_dpdk_libraries
    ...
    dpdk::pmd_virtio
    ...

# apps/CMakelist.txt
    target_link_libraries (${target}
        PRIVATE seastar seastar_private)
```

After linking the virtio library, the application should
try to connect to a vhost. I hard coded this part.

I did not understand how seastars parameter system work.
For this reason I hardcode the virtio configuration in
`dpdk_rte.cc:127`. The code should look like below.

```c++
// Farbod: add some hard coded dpdk parameters
args.push_back(string2vector("--vdev"));
args.push_back(string2vector("virtio_user0,path=/tmp/tmp_vhost0.sock,queues=1"));
args.push_back(string2vector("--no-pci"));
```

After changing the code, recompile. (ninja)
It should not be needed to clean build but documentation purposes ninja -t clean
cleans the build.

The DPDK driver has some assertions which might not let
the application work. we can safely disable offload assertions.
Also, there appears to be a bug in the logic that turns on `LRO / HW_LOC` feature.
Even if not configured, they might be enabled.

