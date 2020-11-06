#!/usr/bin/python

import argparse
import subprocess
import yaml
import paramiko
import time
import multiprocessing
import sys
import os

# Things the wrapper needs to do in order - 
# 1. Launch the shuffle client and write output to a yaml file
# 2. Once written, send it to the shuffle initiator

#Helper function to ssh into remote hosts
def connect_rhost(rhost):
	rssh = paramiko.SSHClient()
	rssh.load_system_host_keys()
	rssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
	rssh.connect(rhost)
	return rssh

def launch_shuffle_client(shuffle_client_args):
	cmd = "~/shuffle/shuffle_client " + shuffle_client_args
	p = subprocess.Popen(cmd, shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	output_lines = []
	if p.stdout is not None:
		#print "INFO: Waiting for shuffle benchmark to get over ..."
		output_lines = p.stdout.readlines()
	print output_lines[0][:-1]
	return output_lines[1:]

def launch_shuffle_clients(shuffle_flow_size, shuffle_size, shuffle_client_args):
	cores = list(range(0,56))
	from random import sample
	shuffle_size = int(shuffle_size)
	selected_cores = sample(cores, int(shuffle_size))
	host_port_pair = []
	shuffle_client_subprocesses = []

	for i in range(0,len(shuffle_client_args),2):
		host_port_pair.append(" ".join([shuffle_client_args[i], shuffle_client_args[i+1]]))

	for i in range(0,shuffle_size):
		cmd = "taskset -c "+ str(selected_cores[i]) + " ~/shuffle/shuffle_client " + shuffle_flow_size + " 1 " + host_port_pair[i]
		#print cmd
		p = subprocess.Popen(cmd, shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
		shuffle_client_subprocesses.append(p)

	exit_codes = [p.wait() for p in shuffle_client_subprocesses]
	result = []
	for p in shuffle_client_subprocesses:
		output_lines = p.stdout.readlines()
		#print output_lines
		if len(result) == 0:
			output_lines = output_lines[1:-1]
		else:
			output_lines = output_lines[2:-1]
		for line in output_lines:
			result.append(line)

	return result

def start_stdout_dstat_proc(run_len):
    dstat_cmd = 'python ~/shuffle/monitorDstat.py --runlen %d' % run_len
    dstat_proc = subprocess.Popen(dstat_cmd, shell=True, stderr=subprocess.STDOUT, stdout=subprocess.PIPE)
    return dstat_proc

def write_results(results, initiator_host, current_host):
	name = current_host.split(".")[-1]+"_node.yaml"
	fp = open(name, "w")
	for line in results:
		fp.write(line)
	fp.close()

def write_dstat_results(results, initiator_host, current_host):
	name = current_host.split(".")[-1]+"_cpu.yaml"
	fp = open(name, "w")
	for line in results:
		fp.write(line)
	fp.close()

def run(initiator_host, current_host, shuffle_flow_size, shuffle_size, shuffle_client_args):

	shuffle_size = int(shuffle_size)
	if shuffle_size == 5:
		run_len = 10
	elif shuffle_size == 11:
		run_len = 20
	else:
		run_len = 40

	dstat_proc = start_stdout_dstat_proc(run_len)

	results = launch_shuffle_clients(shuffle_flow_size, shuffle_size, shuffle_client_args)
	write_results(results, initiator_host, current_host)
	
	if dstat_proc.stdout is not None:
		#print "INFO: Waiting for shuffle benchmark to get over ..."
		dstat_output_lines = dstat_proc.stdout.readlines()

	write_dstat_results(dstat_output_lines, initiator_host, current_host)
	if initiator_host == current_host:
		return

	from random import randint
	time.sleep(randint(1,10))
	rssh_object = connect_rhost(initiator_host)
	#stdin, stdout, stderr = rssh_object.exec_cmd("ls -l")
	#print stdout.readlines()
	rssh_sftp = rssh_object.open_sftp()
	node_name = current_host.split(".")[-1]+"_node.yaml"
	#os.system("scp ./"+node_name+" "+str(initiator_host)+":~/")
	rssh_sftp.put("./"+node_name, "/users/asinghvi/"+node_name)
	time.sleep(1)
	cpu_name = current_host.split(".")[-1]+"_cpu.yaml"
	#os.system("scp ./"+cpu_name+" "+str(initiator_host)+":~/")
	rssh_sftp.put("./"+cpu_name, "/users/asinghvi/"+cpu_name)
	rssh_sftp.close()
	# Cleaning up needs to be done
	return

if __name__ == "__main__":

	initiator_host = sys.argv[1]
	current_host = sys.argv[2]
	shuffle_flow_size = sys.argv[3]
	shuffle_size = sys.argv[4]
	#shuffle_client_args = " ".join(sys.argv[3:])
	run(initiator_host, current_host, shuffle_flow_size, shuffle_size, sys.argv[5:])


