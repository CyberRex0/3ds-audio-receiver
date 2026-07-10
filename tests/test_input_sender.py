import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import send_input


class FakeSoundDevice:
    @staticmethod
    def query_hostapis():
        return [
            {"name": "MME", "default_input_device": 0},
            {"name": "Windows WASAPI", "default_input_device": 2},
        ]

    @staticmethod
    def query_devices():
        return [
            {"name": "MME Mic", "hostapi": 0, "max_input_channels": 1},
            {"name": "Speakers", "hostapi": 1, "max_input_channels": 0},
            {
                "name": "USB Microphone",
                "hostapi": 1,
                "max_input_channels": 1,
                "default_samplerate": 48000.0,
            },
            {
                "name": "Line Input",
                "hostapi": 1,
                "max_input_channels": 2,
                "default_samplerate": 44100.0,
            },
        ]


class InputSenderTests(unittest.TestCase):
    def test_lists_only_wasapi_input_devices(self):
        devices, default_id = send_input.wasapi_input_devices(FakeSoundDevice)
        self.assertEqual([device["index"] for device in devices], [2, 3])
        self.assertEqual(default_id, 2)

    def test_resolves_default_id_and_name_substring(self):
        devices, default_id = send_input.wasapi_input_devices(FakeSoundDevice)
        self.assertEqual(send_input.resolve_device(devices, None, default_id)["index"], 2)
        self.assertEqual(send_input.resolve_device(devices, "line", default_id)["index"], 3)

    def test_mono_pcm_is_duplicated_to_stereo(self):
        mono = b"\x01\x00\xfe\xff"
        self.assertEqual(
            send_input.mono_to_stereo(mono),
            b"\x01\x00\x01\x00\xfe\xff\xfe\xff",
        )

    def test_packetizer_keeps_stereo_frame_boundaries(self):
        packetizer = send_input.Packetizer()
        self.assertEqual(packetizer.add(b"\x00" * 1000), [])
        packets = packetizer.add(b"\x01" * 800)
        self.assertEqual([len(packet) for packet in packets], [1400])
        self.assertEqual(len(packetizer.pending), 400)

    def test_streaming_resampler_preserves_channels_and_phase(self):
        resampler = send_input.LinearResampler(4, 2, 2)
        first = resampler.add(
            b"\x00\x00\x00\x00\x64\x00\xc8\x00"
            b"\xc8\x00\x90\x01\x2c\x01\x58\x02"
        )
        self.assertEqual(first, b"\x00\x00\x00\x00\xc8\x00\x90\x01")
        second = resampler.add(b"\x90\x01\x20\x03\xf4\x01\xe8\x03")
        self.assertEqual(second, b"\x90\x01\x20\x03")


if __name__ == "__main__":
    unittest.main()
