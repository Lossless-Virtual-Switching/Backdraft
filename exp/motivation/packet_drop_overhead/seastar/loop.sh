for i in 8 6 4 2 1
do
	./run_remote.sh $i 1
done

for i in 0.9 0.8 0.7 0.6 0.5
do
	./run_remote.sh 1 $i
done

