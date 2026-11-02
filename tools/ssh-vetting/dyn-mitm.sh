#!/bin/bash
# MITM run: mock car on :6801 + MITM ws proxy :8443 -> real pretzel server.
# Container HAS outbound internet (bridge) so upstream wss works; car leg = mock.
export XDG_RUNTIME_DIR=/tmp/xdg
mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
export DISPLAY=:99
rm -f /work/car-traffic.log /work/relay-traffic.log /work/mitm.log /work/client-stdout.log /work/cap.pcap

Xvfb :99 -screen 0 1280x800x24 &
sleep 2
openbox &
sleep 2
python3 /work/mock_car.py &
sleep 1
python3 /work/mitm_relay.py &
sleep 1
tcpdump -i any -w /work/cap.pcap 'tcp port 6801 or tcp port 8443' &
sleep 1
strace -f -tt -e trace=network,execve,read,write,recvfrom,sendto -o /work/strace2.log \
  bavarian-ssh-client --server ws://127.0.0.1:8443 --insecure \
  > /work/client-stdout.log 2>&1 &
APP=$!
sleep 8

WID=$(xdotool search --name "bavarian" | head -1)
echo "window: $WID"
xdotool windowactivate --sync $WID
sleep 1
xdotool mousemove --window $WID 200 20 click 1
sleep 0.5
xdotool type --clearmodifiers --delay 60 "127.0.0.1"
sleep 0.5
xdotool mousemove --window $WID 575 20 click 1
sleep 20
import -window root /work/ui-mitm.png
kill $APP 2>/dev/null

echo "=========== MITM LOG ==========="
cat /work/mitm.log 2>/dev/null
echo "=========== CAR TRAFFIC ==========="
cat /work/car-traffic.log 2>/dev/null
echo "=========== CLIENT STDOUT ==========="
cat /work/client-stdout.log
