import socket
import time
import threading

host = '192.168.42.1'
port = 10001

class Message:
    def __init__(self, id, data = None, extended: bool = False):
        self.id = id
        self.extended = extended
        self.data = data

    @property
    def dlc(self) -> int:
        return len(self.data)

    def to_bytes(self) -> bytes:
        payload = ':'
        payload += 'X' if self.extended else 'S'
        payload += f'{self.id:03X}'
        payload += 'N'
        payload += ''.join(f'{b:02X}' for b in self.data)
        payload += ';'
        return payload.encode('ASCII')


class Periodic(threading.Thread):
    def __init__(self, message: Message, period: float):
        super().__init__()
        self._stopped = threading.Event()
        self._message = message
        self._period = period


    def run(self):
        while not self._stopped.wait(self._period):
            s.sendall(self._message.to_bytes())

    def stop(self):
        self._stopped.set()


if __name__ == "__main__":
    periodic = Periodic(Message(0x456, [8, 6, 4, 2, 0]), 0.01)
    periodic2 = Periodic(Message(0x56789A, [5, 3, 7, 9], True), 0.1)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        periodic.start()
        periodic2.start()
        s.settimeout(10)
        try:
            while True:
                print('Received:', s.recv(1024))
        except KeyboardInterrupt:
            periodic.stop()
            periodic2.stop()
