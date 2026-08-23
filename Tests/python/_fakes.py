from collections import deque
from collections.abc import Callable

from canloader.j1939 import CanFrame


Responder = Callable[[CanFrame, "FakeBus"], None]


class FakeBus:
    def __init__(self, responder: Responder | None = None) -> None:
        self.responder = responder
        self.sent: list[CanFrame] = []
        self.pending: deque[CanFrame] = deque()
        self.recv_timeouts: list[float] = []

    def send(self, frame: CanFrame) -> None:
        self.sent.append(frame)
        if self.responder is not None:
            self.responder(frame, self)

    def recv(self, timeout: float) -> CanFrame | None:
        self.recv_timeouts.append(timeout)
        return self.pending.popleft() if self.pending else None

    def queue(self, frame: CanFrame) -> None:
        self.pending.append(frame)
