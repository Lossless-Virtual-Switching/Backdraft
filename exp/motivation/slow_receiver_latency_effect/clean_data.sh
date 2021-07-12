file=$1
echo "BG Value Size: $file"
echo "Request Completion Time @99"
cat $file | grep "read" | awk '{print $10}'
echo "Retransmisions"
cat $file | egrep "\snode0:"
