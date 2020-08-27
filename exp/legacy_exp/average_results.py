#!/usr/bin/python3
import argparse


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('input_file')
    parser.add_argument('--output', help='where to store results', default=None)

    args = parser.parse_args()

    infile_path = args.input_file

    with open(infile_path) as infile:
        lines = infile.readlines()

    sums = {
      '99.99': 0,
      '99.9': 0,
      '99': 0,
      'qps': 0,
    }
    maxi = {
      '99.99': float('-inf'),
      '99.9': float('-inf'),
      '99': float('-inf'),
      'qps': float('-inf'),
    }
    count_exp  = 0
    count_lines = len(lines)
    start_line = 21  # counting from zero
    line_delta = 498
    line_num = start_line
    while line_num < count_lines:
        line  = lines[line_num]
        raw = line.split()
        
        for key, index in zip(['99', '99.9', '99.99'], [8, 9, 10]):
            val = float(raw[index])
            sums[key] += val
            if val > maxi[key]:
                maxi[key] = val

        # qps
        raw = lines[line_num + 4].split()
        val = float(raw[3])
        sums['qps'] += val
        if val > maxi['qps']:
            maxi['qps'] = val

        count_exp += 1
        line_num += line_delta

    res = ['average results for: {}'.format(infile_path)]
    for key in sorted(sums.keys()):
      avg = sums[key] / count_exp
      line = '{}: {:.2f}'.format(key, avg)
      res.append(line)
    res.append('maximum results for: {}'.format(infile_path))
    for key in sorted(maxi.keys()):
      m = maxi[key]
      line = '{}: {:.2f}'.format(key, m)
      res.append(line)
    txt = '\n'.join(res)
   
    if args.output:
        with open(args.output, 'w') as outfile:
            outfile.write(txt)
    else:
        print(txt)
             
