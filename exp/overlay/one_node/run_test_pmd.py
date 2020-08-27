#!/usr/bin/python3
import os
import sys
import argparse
import subprocess


def bessctl_do(command, stdout=None):
  """
  Run bessctl commands.
  bessctl_bin is the path to bessctl binary and
  is defined globaly.
  """
  cmd = '{} {}'.format(bessctl_bin, command)
  ret = subprocess.run(cmd, shell=True) # , stdout=subprocess.PIPE
  return ret


def setup_bess_pipeline(pipeline_config_path):
  # Make sure bessctl daemon is down
  bessctl_do('daemon stop')

  # Run BESS config
  ret = bessctl_do('daemon start')
  if ret.returncode != 0:
    print('failed to start bess daemon', file=sys.stderr)
    return -1
  # Run a configuration (pipeline)
  ret = bessctl_do('daemon start -- run file {}'.format(pipeline_config_path))


def main():
  setup_bess_pipeline(pipeline_file)
  if args.bessonly:
    print('only BESS pipeline has been setuped')
    return 0
  pass


if __name__ == '__main__':
  cur_script_dir = os.path.dirname(os.path.abspath(__file__))
  bessctl_dir = os.path.abspath(
          os.path.join(cur_script_dir, '../../../code/bess-v1/bessctl'))
  bessctl_bin = os.path.join(bessctl_dir, 'bessctl')
  pipeline_file = './single_node_test.bess'

  # args
  parser = argparse.ArgumentParser(description='Use dpdk testpmd to create flows moving in a pipeline')
  parser.add_argument('-bessonly', help='only setup BESS pipeline',
    action='store_true', default=False)
  args = parser.parse_args()

  main()

