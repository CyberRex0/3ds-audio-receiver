import io
import pathlib
import sys
import unittest
import wave

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import send_audio


class FakeSocket:
    def __init__(self):
        self.calls = []

    def sendto(self, payload, destination):
        self.calls.append((payload, destination))
        return len(payload)


class SenderTests(unittest.TestCase):
    def test_raw_packetization_keeps_frame_boundaries(self):
        data = bytes(range(200)) * 10
        packets = list(send_audio.raw_packets(io.BytesIO(data)))
        self.assertEqual([len(packet) for packet in packets], [1400, 600])
        self.assertEqual(b"".join(packets), data)

    def test_raw_rejects_incomplete_frame(self):
        with self.assertRaisesRegex(ValueError, "4-byte"):
            list(send_audio.raw_packets(io.BytesIO(b"abc")))

    def test_wav_validation_and_packetization(self):
        stream = io.BytesIO()
        with wave.open(stream, "wb") as writer:
            writer.setnchannels(2)
            writer.setsampwidth(2)
            writer.setframerate(send_audio.SAMPLE_RATE)
            writer.writeframes(b"\x00" * 1600)
        stream.seek(0)
        with wave.open(stream, "rb") as reader:
            packets = list(send_audio.wav_packets(reader))
        self.assertEqual([len(packet) for packet in packets], [1400, 200])

    def test_wav_rejects_wrong_rate(self):
        stream = io.BytesIO()
        with wave.open(stream, "wb") as writer:
            writer.setnchannels(2)
            writer.setsampwidth(2)
            writer.setframerate(44100)
            writer.writeframes(b"\x00" * 4)
        stream.seek(0)
        with wave.open(stream, "rb") as reader:
            with self.assertRaisesRegex(ValueError, "32728"):
                list(send_audio.wav_packets(reader))

    def test_send_realtime_uses_destination_and_paces(self):
        fake_socket = FakeSocket()
        times = iter([10.0, 10.0, 10.005])
        sleeps = []
        packets, frames = send_audio.send_realtime(
            [b"\x00" * 1400, b"\x00" * 400],
            fake_socket,
            ("192.0.2.5", 6000),
            clock=lambda: next(times),
            sleeper=sleeps.append,
        )
        self.assertEqual((packets, frames), (2, 450))
        self.assertEqual([call[1] for call in fake_socket.calls], [("192.0.2.5", 6000)] * 2)
        self.assertEqual(len(sleeps), 2)
        self.assertGreater(sleeps[0], 0)
        self.assertGreater(sleeps[1], 0)


if __name__ == "__main__":
    unittest.main()
