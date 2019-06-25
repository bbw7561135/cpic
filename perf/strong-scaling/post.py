import sh
import math
from io import StringIO

BASE_CONF = 'base.conf'
BASE_JOB = 'base.job'

P_list = [1,2,4,8,16,32]
CP_list = [1,2,4,8,16,32,48]
Nc_list = [32,64,128]


def get_field(line, name):
	for c in line.split():
		w = c.split('=')
		if w[0] == name:
			return w[1]

	return None

for CP in CP_list:
	for Nc in Nc_list:

		out = 'csv/CP%d-Nc%d' % (CP, Nc)

		t0 = 0
		with open(out, 'w+') as f:

			f.write("P\tmean\trel-err\tspeedup\tefficiency\n")

			for P in P_list:

				C = P*CP
				N = int(math.ceil(C / 48))

				name = 'P%d-CP%d-Nc%d' % (P, CP, Nc)
				out = 'out/' + name
				print('Reading %s' % out)

				try:
					#sh.tail('-1', sh.grep('^stats', out), _out=buf)
					buf = StringIO()
					sh.tail(sh.grep('^stats', out), '-1', _out=buf)
					line = buf.getvalue()
				except:
					line = ""

				if line == "": continue

				mean = float(get_field(line, "mean"))
				sem = float(get_field(line, "sem"))
				rel_error = sem * 1.96 / mean
				if P == 1:
					speedup = 1
					t0 = mean
				else:
					speedup = t0 / mean

				efficiency = speedup / P

				f.write("%d\t%e\t%e\t%e\t%e\n" %
						(P, mean, rel_error, speedup, efficiency))
