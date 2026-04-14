import socket

s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM) 
s.bind(('0.0.0.0',50000))

print('listening on 50000');

while True:
    d,a=s.recvfrom(2048); print(d.decode('ascii','replace').rstrip())