#!/usr/bin/env python3
"""Capture a Windows WASAPI input and stream PCM audio to a 3DS over UDP."""

from __future__ import annotations

import argparse
import queue
import socket
import sys
from array import array
from collections.abc import Iterable
from typing import Any

from send_audio import CHANNELS, FRAME_BYTES, PACKET_BYTES, SAMPLE_RATE


def wasapi_input_devices(sd: Any) -> tuple[list[dict[str, Any]], int]:
    """Return WASAPI input devices and the WASAPI default input device ID."""
    hostapis = sd.query_hostapis()
    wasapi_ids = [
        index
        for index, hostapi in enumerate(hostapis)
        if "WASAPI" in str(hostapi["name"]).upper()
    ]
    if not wasapi_ids:
        raise RuntimeError("Windows WASAPI host API was not found")

    default_ids = [
        int(hostapis[index].get("default_input_device", -1)) for index in wasapi_ids
    ]
    devices: list[dict[str, Any]] = []
    for index, device in enumerate(sd.query_devices()):
        if int(device["hostapi"]) not in wasapi_ids:
            continue
        if int(device["max_input_channels"]) <= 0:
            continue
        entry = dict(device)
        entry["index"] = index
        devices.append(entry)
    return devices, next((item for item in default_ids if item >= 0), -1)


def resolve_device(
    devices: Iterable[dict[str, Any]], requested: str | None, default_id: int
) -> dict[str, Any]:
    candidates = list(devices)
    if not candidates:
        raise ValueError("no WASAPI input devices are available")

    if requested is None:
        for device in candidates:
            if device["index"] == default_id:
                return device
        raise ValueError("WASAPI has no default input device; specify --device")

    try:
        requested_id = int(requested)
    except ValueError:
        requested_id = None
    if requested_id is not None:
        for device in candidates:
            if device["index"] == requested_id:
                return device
        raise ValueError(f"WASAPI input device {requested_id} was not found")

    folded = requested.casefold()
    exact = [device for device in candidates if str(device["name"]).casefold() == folded]
    matches = exact or [
        device for device in candidates if folded in str(device["name"]).casefold()
    ]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise ValueError(f"WASAPI input device matching {requested!r} was not found")
    ids = ", ".join(str(device["index"]) for device in matches)
    raise ValueError(f"device name is ambiguous (matching IDs: {ids})")


def mono_to_stereo(data: bytes) -> bytes:
    if len(data) % 2:
        raise ValueError("mono PCM16 data is not sample-aligned")
    samples = array("h")
    samples.frombytes(data)
    stereo = array("h", [0]) * (len(samples) * CHANNELS)
    stereo[0::2] = samples
    stereo[1::2] = samples
    return stereo.tobytes()


class Packetizer:
    """Accumulate PCM frames without splitting one across UDP datagrams."""

    def __init__(self) -> None:
        self.pending = bytearray()

    def add(self, data: bytes) -> list[bytes]:
        if len(data) % FRAME_BYTES:
            raise ValueError("captured PCM data is not stereo-frame-aligned")
        self.pending.extend(data)
        packets = []
        while len(self.pending) >= PACKET_BYTES:
            packets.append(bytes(self.pending[:PACKET_BYTES]))
            del self.pending[:PACKET_BYTES]
        return packets


class LinearResampler:
    """Small streaming PCM16 resampler that retains phase between callbacks."""

    def __init__(self, input_rate: int, output_rate: int, channels: int) -> None:
        if input_rate <= 0 or output_rate <= 0 or channels <= 0:
            raise ValueError("sample rates and channel count must be positive")
        self.channels = channels
        self.step = input_rate / output_rate
        self.position = 0.0
        self.samples = array("h")

    def add(self, data: bytes) -> bytes:
        if len(data) % (self.channels * 2):
            raise ValueError("captured PCM16 data is not frame-aligned")
        incoming = array("h")
        incoming.frombytes(data)
        self.samples.extend(incoming)

        output = array("h")
        frame_count = len(self.samples) // self.channels
        while self.position + 1 < frame_count:
            left = int(self.position)
            fraction = self.position - left
            left_offset = left * self.channels
            right_offset = left_offset + self.channels
            for channel in range(self.channels):
                first = self.samples[left_offset + channel]
                second = self.samples[right_offset + channel]
                value = round(first + (second - first) * fraction)
                output.append(max(-32768, min(32767, value)))
            self.position += self.step

        consumed = int(self.position)
        if consumed:
            del self.samples[: consumed * self.channels]
            self.position -= consumed
        return output.tobytes()


class CaptureQueue:
    def __init__(self, capacity: int = 16) -> None:
        self.items: queue.Queue[bytes] = queue.Queue(maxsize=capacity)
        self.dropped_frames = 0
        self.status_messages: queue.SimpleQueue[str] = queue.SimpleQueue()

    def callback(self, indata: Any, frames: int, _time: Any, status: Any) -> None:
        if status:
            self.status_messages.put(str(status))
        try:
            self.items.put_nowait(bytes(indata))
        except queue.Full:
            self.dropped_frames += frames


def print_devices(devices: Iterable[dict[str, Any]], default_id: int) -> None:
    print("WASAPI input devices:")
    for device in devices:
        marker = " *" if device["index"] == default_id else ""
        print(
            f"  {device['index']:>3}: {device['name']} "
            f"({device['max_input_channels']} ch, "
            f"{device['default_samplerate']:.0f} Hz){marker}"
        )
    print("  * = WASAPI default input device")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("host", nargs="?", help="IP address shown by the 3DS application")
    parser.add_argument("--port", type=int, default=5000, help="UDP port (default: 5000)")
    parser.add_argument("--device", help="WASAPI input device ID or unique name substring")
    parser.add_argument(
        "--list-devices", action="store_true", help="list WASAPI input devices and exit"
    )
    args = parser.parse_args(argv)
    if not args.list_devices and not args.host:
        parser.error("host is required unless --list-devices is used")
    if not 1024 <= args.port <= 65535:
        parser.error("--port must be between 1024 and 65535")
    return args


def load_sounddevice() -> Any:
    if sys.platform != "win32":
        raise RuntimeError("this tool only supports Windows")
    try:
        import sounddevice as sd
    except ImportError as exc:
        raise RuntimeError(
            "python-sounddevice is required; run `uv sync` from the project root"
        ) from exc
    return sd


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    sd = None
    try:
        sd = load_sounddevice()
        devices, default_id = wasapi_input_devices(sd)
        if args.list_devices:
            print_devices(devices, default_id)
            return 0

        device = resolve_device(devices, args.device, default_id)
        input_channels = min(CHANNELS, int(device["max_input_channels"]))
        input_rate = round(float(device["default_samplerate"]))
        if input_rate <= 0:
            raise ValueError("selected device has no valid default sample rate")
        capture = CaptureQueue()
        resampler = LinearResampler(input_rate, SAMPLE_RATE, input_channels)
        packetizer = Packetizer()
        destination = (args.host, args.port)
        packets_sent = 0
        frames_sent = 0

        settings = sd.WasapiSettings(exclusive=False, auto_convert=True)
        print(
            f"capturing {device['name']} (ID {device['index']}) via WASAPI; "
            f"{input_rate} Hz -> {SAMPLE_RATE} Hz; sending to {args.host}:{args.port}"
        )
        print("Press Ctrl+C to stop.")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
            with sd.RawInputStream(
                samplerate=input_rate,
                blocksize=0,
                device=device["index"],
                channels=input_channels,
                dtype="int16",
                latency="low",
                callback=capture.callback,
                extra_settings=settings,
            ):
                try:
                    while True:
                        try:
                            chunk = capture.items.get(timeout=0.5)
                        except queue.Empty:
                            continue
                        chunk = resampler.add(chunk)
                        if input_channels == 1:
                            chunk = mono_to_stereo(chunk)
                        for packet in packetizer.add(chunk):
                            udp_socket.sendto(packet, destination)
                            packets_sent += 1
                            frames_sent += len(packet) // FRAME_BYTES
                        while not capture.status_messages.empty():
                            print(
                                f"warning: WASAPI: {capture.status_messages.get_nowait()}",
                                file=sys.stderr,
                            )
                except KeyboardInterrupt:
                    pass
            if packetizer.pending:
                udp_socket.sendto(bytes(packetizer.pending), destination)
                packets_sent += 1
                frames_sent += len(packetizer.pending) // FRAME_BYTES
                packetizer.pending.clear()
    except Exception as exc:
        expected = (OSError, RuntimeError, ValueError)
        if sd is not None:
            expected += (sd.PortAudioError,)
        if not isinstance(exc, expected):
            raise
        print(f"error: {exc}", file=sys.stderr)
        return 1

    seconds = frames_sent / SAMPLE_RATE
    print(
        f"stopped; sent {packets_sent} packets, {frames_sent} frames ({seconds:.2f} s), "
        f"dropped {capture.dropped_frames} capture frames"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
