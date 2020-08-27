import os
import subprocess
from unittest import TestCase


class TestBessCompile(TestCase):

  def test_bess_compile(self):
			cur_script_dir = os.path.dirname(__file__)
			bess_dir = os.path.join(cur_script_dir, '../code/bess-v1/')
			bess_dir = os.path.abspath(bess_dir)
			# print(bess_dir)

			os.chdir(bess_dir)
			r = subprocess.run('./build.py bess', shell=True, stdout=subprocess.PIPE)
			# msg = r.stdout.decode().strip()
			# expected = """
			# Generating protobuf codes for pybess...
			# Building BESS daemon...
			# """.strip()
			print(r.returncode)
			expected = 0
			value = r.returncode
			self.assertEqual(expected, value)
			print('done')


