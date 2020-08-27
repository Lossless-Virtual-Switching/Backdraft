#!/bin/sh

if [ $# -lt 1 ]
then 
  echo "expecting one argument (name)"
  exit 1
fi
name=$1

script_directory=$(realpath $(dirname $0))

sudo docker run -d \
  --name $name \
  -v $script_directory/httpd.conf:/usr/local/apache2/conf/httpd.conf \
  -v $script_directory/website/:/usr/local/apache2/htdocs/ \
  my_httpd

# sudo docker run -d \
#   --name $name \
#   -v $script_directory/website/:/usr/local/apache2/htdocs/ \
#   httpd:2.4
