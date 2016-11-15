#!/bin/bash
for i in `seq 0 24`; do
   openssl enc -aes-256-ctr -pass pass:"$(dd if=/dev/urandom bs=128 count=1 2>/dev/null | base64)" -nosalt </dev/zero | dd iflag=fullblock bs=1048576 count=16384 of=bench-r$i & 
done
wait
