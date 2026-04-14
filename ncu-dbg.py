import socket
from datetime import datetime

s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM) 
s.bind(('0.0.0.0',50000))

print('listening on 50000');

while True:
    d,a=s.recvfrom(2048); print(f"[{datetime.now()}] {d.decode('ascii','replace').rstrip()}")