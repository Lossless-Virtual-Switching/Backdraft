dir=$1
files=`ls $dir`
res=""
for f in $files
do
	# echo $f
	path="$dir/$f"
	burst=`echo $f | cut -d "." -f 1 | cut -d "b" -f 2`
	lat=`grep @99] $path  | awk '{print $3}'`
	if [ $lat = "" ]; then continue; fi
	tp=`grep TP $path | awk '{ if ($2 > 1000) {sum += $2; n++} } END { if (n > 0) printf ("%f\n", sum / n / 1000000); }'`
	# echo "$f $lat"
	res="$res`printf "%15s | %8d | %15.2f | %15.2f" "$f" $burst $lat $tp`\n"

done
printf "%15s | %8s | %15s | %15s\n" "File" "Burst" "RCT @99" "Throughput (Mpps)"
printf "%53s\n" "-------------------------------------------"
printf "$res" | sort -k 3 -n

echo
