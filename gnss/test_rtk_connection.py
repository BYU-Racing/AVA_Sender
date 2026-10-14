"""Exercise the RTK WebSocket link without GNSS hardware."""

import socket
import threading
import time
import unittest
from contextlib import ExitStack, contextmanager
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

from websockets.exceptions import InvalidStatus
from websockets.sync.client import connect
from websockets.sync.server import serve

from gnss import gnss_base_station as base
from gnss import gnss_rtk_test as rover


@contextmanager
def running_server(port=0):
    relay = base.RTKServer()
    with serve(
        relay.handle_rover, "127.0.0.1", port, process_request=relay.process_request
    ) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            yield relay, server.socket.getsockname()[1]
        finally:
            server.shutdown()
            worker.join(timeout=2)


def wait_for_clients(relay, count):
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        with relay.lock:
            if len(relay.clients) == count:
                return
        time.sleep(0.01)
    raise AssertionError(f"Expected {count} connected rovers")


class RTKConnectionTests(unittest.TestCase):
    def test_binary_broadcast_and_rover_reconnection(self):
        with running_server() as (relay, port):
            url = f"ws://127.0.0.1:{port}/rtk"
            self.assertEqual(relay.broadcast(b"no rover yet"), 0)
            with connect(url, proxy=None) as first, connect(url, proxy=None) as second:
                wait_for_clients(relay, 2)
                frame = b"\xd3\x00\x03\x00\xff\x80"
                self.assertEqual(relay.broadcast(frame), 2)
                self.assertEqual(first.recv(timeout=2), frame)
                self.assertEqual(second.recv(timeout=2), frame)
            wait_for_clients(relay, 0)
            with connect(url, proxy=None) as reconnected:
                wait_for_clients(relay, 1)
                relay.broadcast(b"new corrections")
                self.assertEqual(reconnected.recv(timeout=2), b"new corrections")

    def test_only_rtk_route_is_accepted(self):
        with running_server() as (_, port):
            with self.assertRaises(InvalidStatus) as caught:
                connect(f"ws://127.0.0.1:{port}/wrong", proxy=None)
            self.assertEqual(caught.exception.response.status_code, 404)

    def test_rover_waits_for_base_and_recovers_after_restart(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        url = f"ws://127.0.0.1:{port}/rtk"
        attempted = threading.Event()
        received = [threading.Event(), threading.Event()]
        abort = threading.Event()
        failures = []
        serial_port = MagicMock()
        frames = []

        def write(data):
            frames.append(data)
            received[len(frames) - 1].set()
            if len(frames) == 2:
                raise KeyboardInterrupt

        def connect_to_base(*args, **kwargs):
            try:
                return connect(*args, open_timeout=0.5, **kwargs)
            except OSError:
                attempted.set()
                raise

        def read_status(reader, stopped):
            while not stopped.wait(0.01):
                if abort.is_set():
                    stopped.set()

        def run_rover():
            try:
                rover.main()
            except KeyboardInterrupt:
                pass
            except BaseException as exc:
                failures.append(exc)

        serial_port.write.side_effect = write
        with (
            patch.object(rover, "Serial", return_value=serial_port),
            patch.object(rover, "UBXReader"),
            patch.object(rover, "config_ubx"),
            patch.object(rover, "read_from_ubx", side_effect=read_status),
            patch.object(rover, "connect", side_effect=connect_to_base),
            patch("sys.argv", ["gnss_rtk_test.py", "--ws-url", url]),
        ):
            worker = threading.Thread(target=run_rover, daemon=True)
            worker.start()
            try:
                self.assertTrue(attempted.wait(2))
                for index, frame in enumerate((b"first correction", b"after restart")):
                    with running_server(port) as (relay, _):
                        wait_for_clients(relay, 1)
                        relay.broadcast(frame)
                        self.assertTrue(received[index].wait(2))
            finally:
                abort.set()
                worker.join(timeout=3)
            self.assertFalse(worker.is_alive())
            self.assertEqual(failures, [])
            self.assertEqual(frames, [b"first correction", b"after restart"])
            serial_port.close.assert_called_once()

    def test_base_accepts_rover_during_survey_and_then_forwards_rtcm(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        serial_context = MagicMock()
        serial_port = serial_context.__enter__.return_value
        reader = MagicMock()
        client = None
        client_context = ExitStack()
        calls = 0
        frame = b"\xd3\x00\x03\x00\xff\x80"

        def read():
            nonlocal client, calls
            calls += 1
            if calls == 1:
                client = client_context.enter_context(
                    connect(f"ws://127.0.0.1:{port}/rtk", proxy=None)
                )
                with self.assertRaises(TimeoutError):
                    client.recv(timeout=0.05)
                return b"survey", SimpleNamespace(
                    identity="NAV-SVIN", dur=300, meanAcc=10000, obs=300,
                    active=0, valid=1, meanXHP=0, meanYHP=0, meanZHP=0,
                )
            if calls == 2:
                return frame, SimpleNamespace(identity="1005")
            self.assertEqual(client.recv(timeout=2), frame)
            raise KeyboardInterrupt

        reader.read.side_effect = read
        with (
            patch.object(base, "Serial", return_value=serial_context),
            patch.object(base, "UBXReader", return_value=reader),
            patch("sys.argv", ["gnss_base_station.py", "--host", "127.0.0.1",
                               "--ws-port", str(port)]),
        ):
            try:
                with self.assertRaises(KeyboardInterrupt):
                    base.main()
            finally:
                client_context.close()
        serial_port.write.assert_called_once()
        serial_context.__exit__.assert_called_once()


if __name__ == "__main__":
    unittest.main()
