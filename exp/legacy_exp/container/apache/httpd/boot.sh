#!/bin/sh

# sleep some time until BESS connects VPort
sleep 10

exec httpd-foreground

