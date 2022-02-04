CLIENT_COUNT=29
SERVER_COUNT=1

python3 ./setup_single_node_exp.py --count_queue 1 --queue_size 512 \
    $CLIENT_COUNT $SERVER_COUNT

