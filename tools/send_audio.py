#!/usr/bin/env python3
"""Send 32728 Hz signed PCM16LE stereo audio to a 3DS over raw UDP."""

from __future__ import annotations

import argparse
import contextlib
import socket
import sys
import time
import wave
from collections.abc import Iterable, Iterator
from typing import BinaryIO

SAMPLE_RATE = 32728
CHANNELS = 2
SAMPLE_WIDTH = 2
FRAME_BYTES = CHANNELS * SAMPLE_WIDTH
PACKET_BYTES = 1400
FRAMES_PER_PACKET = PACKET_BYTES // FRAME_BYTES


def raw_packets(stream: BinaryIO) -> Iterator[bytes]:
    pending = b""
    while True:
        chunk = stream.read(PACKET_BYTES - len(pending))
        if not chunk:
            break
        pending += chunk
        if len(pending) == PACKET_BYTES:
            yield pending
            pending = b""
    if pending:
        if len(pending) % FRAME_BYTES:
            raise ValueError("raw input size is not aligned to a 4-byte stereo frame")
        yield pending


def wav_packets(reader: wave.Wave_read) -> Iterator[bytes]:
    if reader.getnchannels() != CHANNELS:
        raise ValueError("WAV must be stereo")
    if reader.getsampwidth() != SAMPLE_WIDTH:
        raise ValueError("WAV must use 16-bit PCM")
    if reader.getframerate() != SAMPLE_RATE:
        raise ValueError(f"WAV sample rate must be {SAMPLE_RATE} Hz")
    if reader.getcomptype() != "NONE":
        raise ValueError("WAV must be uncompressed PCM")

    while True:
        packet = reader.readframes(FRAMES_PER_PACKET)
        if not packet:
            break
        if len(packet) > PACKET_BYTES or len(packet) % FRAME_BYTES:
            raise ValueError("WAV returned an invalid PCM frame")
        yield packet


def send_realtime(
    packets: Iterable[bytes],
    udp_socket: socket.socket,
    destination: tuple[str, int],
    *,
    clock=time.perf_counter,
    sleeper=time.sleep,
) -> tuple[int, int]:
    start = clock()
    frames_sent = 0
    packets_sent = 0
    for packet in packets:
        if not packet or len(packet) > PACKET_BYTES or len(packet) % FRAME_BYTES:
            raise ValueError("packet must contain 1..1400 bytes aligned to PCM frames")
        udp_socket.sendto(packet, destination)
        packets_sent += 1
        frames_sent += len(packet) // FRAME_BYTES
        deadline = start + frames_sent / SAMPLE_RATE
        remaining = deadline - clock()
        if remaining > 0:
            sleeper(remaining)
    return packets_sent, frames_sent


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="PCM WAV/raw file, or - for raw stdin")
    parser.add_argument("host", help="IP address shown by the 3DS application")
    parser.add_argument("--port", type=int, default=5000, help="UDP port (default: 5000)")
    parser.add_argument("--raw", action="store_true", help="treat input as headerless PCM")
    parser.add_argument("--loop", action="store_true", help="repeat a file until interrupted")
    args = parser.parse_args(argv)
    if not 1024 <= args.port <= 65535:
        parser.error("--port must be between 1024 and 65535")
    if args.input == "-" and not args.raw:
        parser.error("stdin requires --raw")
    if args.input == "-" and args.loop:
        parser.error("stdin cannot be looped")
    return args


def packet_source(path: str, raw: bool) -> contextlib.AbstractContextManager[Iterable[bytes]]:
    if path == "-":
        return contextlib.nullcontext(raw_packets(sys.stdin.buffer))
    if raw:
        return _raw_file_packets(path)
    return _wav_file_packets(path)


@contextlib.contextmanager
def _raw_file_packets(path: str) -> Iterator[Iterable[bytes]]:
    with open(path, "rb") as stream:
        yield raw_packets(stream)


@contextlib.contextmanager
def _wav_file_packets(path: str) -> Iterator[Iterable[bytes]]:
    with wave.open(path, "rb") as reader:
        yield wav_packets(reader)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    destination = (args.host, args.port)
    total_packets = 0
    total_frames = 0
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp_socket:
            while True:
                with packet_source(args.input, args.raw) as packets:
                    sent_packets, sent_frames = send_realtime(
                        packets, udp_socket, destination
                    )
                total_packets += sent_packets
                total_frames += sent_frames
                if sent_frames == 0 and args.loop:
                    raise ValueError("cannot loop an empty input")
                if not args.loop:
                    break
    except (OSError, ValueError, wave.Error) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nstopped", file=sys.stderr)

    seconds = total_frames / SAMPLE_RATE
    print(f"sent {total_packets} packets, {total_frames} frames ({seconds:.2f} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
