# Throughput variability

## Memcached 

It is same as describe here [link](./02_resource_scheduling_important.md).
Set variable `EXPERIMENT` in the script to `$ETC`.

Gather results and use `analysis.py` to calculate throughput.

## Nginx

### Prepare NSDI'21

Setup `Nginx` to serve `NSDI'21` webpage.
Use `nginx.conf` as configuration file and add `nginx_nsdi21_site_conf` 
to the available sites. The `NSDI'21` pages are available in 
`/exp/motivation/variable_throughput/nsdi`.

```
sudo cp -r /exp/motivation/variable_throughput/nsdi/www.usenix.org/  /var/www/html/
sudo cp /exp/motivation/variable_throughput/nginx.conf /etc/nginx/nginx.conf
sudo cp /exp/motivation/variable_throughput/nginx_nsdi21_site_conf /etc/nginx/sites-available
# enable the site
sudo ln -sf /etc/nginx/sites-available /etc/nginx/sites-enabled
sudo nginx -s reload
```

### Prepare workload generator

On the workload generator node:

Edit `nginx_exp.sh` and set `server_ip` variable.

If there is a need for multiple workload machines add the ip-address of 
nodes to the `MACHINES`.

Get a modified version of `httpd` that records timestamp of requests from 
[https://github.com/Lossless-Virtual-Switching/httpd-ab.git](https://github.com/Lossless-Virtual-Switching/httpd-ab.git)
and build it. Then set the `ab` variable at line 115 to the path of Apache bench binary.

> TODO: Update the experiment script to prepare the test environment.



### Results

Results will be at `/tmp/nginx_exp_multinode`.

