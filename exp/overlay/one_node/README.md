# Purpose of these scripts
These scripts should setup a pipeline on a single node which is good for testing BESS modules' behaviours.

*note: For forwarding back packets testpmd should be used
```
sudo ./testpmd -l 8,10 -n 2 --no-pci --vdev="virtio_user1,path=/tmp/tmp_vhost1.sock" -- -i --nb-cores=1
> start
```
TODO: Run testpmd from script.
