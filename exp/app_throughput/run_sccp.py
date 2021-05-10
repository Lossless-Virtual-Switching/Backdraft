#!/usr/bin/python3
import argparse
import os
from shutil import rmtree
import subprocess
import sys
import threading
import time


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', '-s',  default='192.168.1.1')
    parser.add_argument('--user', '-u', default='fshahi5')
    parser.add_argument('--port', '-p', type=int, default=22)
    parser.add_argument('--target-path', default='/proj/uic-dcs-PG0/fshahi5/files/')
    parser.add_argument('--base', default='/tmp/')
    parser.add_argument('--num-thread', '-t', type=int, default=1)
    parser.add_argument('--count-files', type=int, default=10)
    parser.add_argument('--large', '-l', action='store_true')
    args = parser.parse_args()
    return args


def scp_client(index, args):
    name = 'scp_worker_{}'.format(index)
    path = os.path.join(args.base, name)
    if os.path.isdir(path):
        rmtree(path)
    os.mkdir(path)
    os.chdir(path)

    k = 0
    while not kill:
        if args.large:
            filename = 'large.img'
        else:
            filename = '{}.txt'.format(k)
        filepath = os.path.join(args.target_path, filename)
        # cmd = 'scp scp://{user}@{server}:{port}/{path} ./'
        cmd = 'scp {user}@{server}:/{path} ./'
        cmd = cmd.format(user=args.user, server=args.server,
                port=args.port, path=filepath)
        # print (cmd)
        try:
            subprocess.check_output(cmd, shell=True)
        except Exception as e:
            #print(e.__cause__, file=sys.stderr)
            break
        k = (k + 1) % args.count_files
        # time.sleep(1) # just for testing
    print('worker ', index, 'done!')


def main():
    args = parse_args()
    if not os.path.isdir(args.base):
        print('base directory not found', file=sys.stderr)
        sys.exit(1)

    subprocess.run('sudo pkill -KILL scp', shell=True)

    for i in range(args.num_thread):
        name = 'worker_{}'.format(i)
        t_args=[i, args]
        t = threading.Thread(target=scp_client, name=name, args=t_args)
        threads.append(t)
        t.start()

    for t in threads:
        t.join()
    print('Stopped')


if __name__ == '__main__':
    kill = False
    try:
        threads = []
        main()
    except KeyboardInterrupt as e:
        kill = True
