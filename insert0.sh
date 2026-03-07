#echo insert 0 -rw ~/pistorm64/data/adfs/JUGGLER.adf | nc localhost 23890
echo insert 0 -rw $1  | nc localhost 23890
