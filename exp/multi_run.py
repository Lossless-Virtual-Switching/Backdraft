#!/usr/bin/python3
"""
This scripts is a helper tool for running a program (in this context an experiment)
for multiple times and store its results in a file.
"""
import os
import subprocess
import argparse


def run_program(program, args):
    cmd = '{} {}'.format(program, args)
    print(cmd)
    res = subprocess.check_output(cmd, shell=True)
    res = res.decode()
    return res


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('program', help='path to the program that should be run')
    parser.add_argument('args', help='args given to the program (soround in double-qoutes)')
    parser.add_argument('output', help='result file path')
    parser.add_argument('--repeat', type=int, default=5,
        help='how many times should an experiment be repeated')

    args = parser.parse_args()
    program = args.program 
    arguments = args.args
    output = args.output
    repeat = args.repeat

    results = []
    for i in range(repeat):
       print ('run ' + str(i))
       results.append('run ' + str(i))
       logs = run_program(program, arguments)
       results.append(logs)

    txt = '\n'.join(results)
    with open(output, 'w') as outfile:
        outfile.write(txt)
    print('results stored in {}'.format(output))

