#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 the OpenRAC contributors
"""
A network impairment proxy between one game and the server, TCP and UDP on the same port.

  netimp.py --listen 127.0.1.1:7770 --server 127.0.0.2:7770 --loss 5 --latency 50 --jitter 10 \
            --reorder 2 --duplicate 1 --seed 1 --stats out.json

The mod opens its UDP socket to the TCP peer's address at the port the server offers, so the proxy
listens on an address of its own at the server's port. One proxy per game: each gets its own link.

UDP: each direction is delayed --latency ms plus up to --jitter ms, --loss % of datagrams are lost,
--reorder % get a further 2 to 4 x --jitter ms (+20) so that later ones pass them, and --duplicate %
arrive twice. TCP: the same delay, in order; --loss % of the segments stall the stream as a
retransmission would (300 ms, or twice the latency when that is more) and nothing behind one
overtakes it.

The proxy counts what it sees by the mod's DATA kind (the first byte of a game payload), both ways,
in messages and bytes, for UDP and TCP, and writes them as JSON to --stats on SIGTERM and every
5 seconds.
"""
import argparse
import asyncio
import json
import random
import signal
import socket
import struct
import time
from collections import defaultdict

DIRECTIONS = ("up", "down")


class Stats:
    def __init__(self):
        self.count = defaultdict(int)
        self.bytes = defaultdict(int)
        self.started = time.monotonic()
        self.lost = defaultdict(int)
        self.duplicated = 0
        self.reordered = 0

    def note(self, transport, direction, kind, size):
        key = f"{transport}/{direction}/{kind}"
        self.count[key] += 1
        self.bytes[key] += size

    def report(self):
        return {
            "seconds": time.monotonic() - self.started,
            "count": dict(self.count),
            "bytes": dict(self.bytes),
            "lost": dict(self.lost),
            "duplicated": self.duplicated,
            "reordered": self.reordered,
        }


class Impairment:
    def __init__(self, args):
        self.loss = args.loss / 100.0
        self.latency = args.latency / 1000.0
        self.jitter = args.jitter / 1000.0
        self.reorder = args.reorder / 100.0
        self.duplicate = args.duplicate / 100.0
        self.random = random.Random(args.seed)

    def delay(self):
        return self.latency + self.random.random() * self.jitter


class Udp(asyncio.DatagramProtocol):
    """The game's side of UDP: one socket towards the server per game address seen."""

    def __init__(self, loop, server, impair, stats):
        self.loop = loop
        self.server = server
        self.impair = impair
        self.stats = stats
        self.transport = None
        self.client = None
        self.upstream = None

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        if addr != self.client:
            self.client = addr
            if self.upstream is not None:
                self.upstream.close()
            self.upstream = None
            self.loop.create_task(self.link())
        kind = "bind" if len(data) < 17 else data[17] if len(data) > 17 else "bind"
        self.stats.note("udp", "up", kind, len(data))
        self.forward(data, self.to_server, "up")

    async def link(self):
        transport, _ = await self.loop.create_datagram_endpoint(
            lambda: Upstream(self), remote_addr=self.server
        )
        self.upstream = transport

    def to_server(self, data):
        if self.upstream is not None:
            self.upstream.sendto(data)

    def to_client(self, data):
        if self.client is not None:
            self.transport.sendto(data, self.client)

    def forward(self, data, send, direction):
        impair = self.impair
        if impair.random.random() < impair.loss:
            self.stats.lost[f"udp/{direction}"] += 1
            return
        delay = impair.delay()
        if impair.random.random() < impair.reorder:
            delay += 0.02 + (2 + 2 * impair.random.random()) * impair.jitter
            self.stats.reordered += 1
        self.loop.call_later(delay, send, data)
        if impair.random.random() < impair.duplicate:
            self.stats.duplicated += 1
            self.loop.call_later(delay + impair.random.random() * 0.01, send, data)


class Upstream(asyncio.DatagramProtocol):
    def __init__(self, udp):
        self.udp = udp

    def datagram_received(self, data, addr):
        kind = data[5] if len(data) > 5 else "none"
        self.udp.stats.note("udp", "down", kind, len(data))
        self.udp.forward(data, self.udp.to_client, "down")


class Frames:
    """Counts the frames of one TCP direction by DATA kind."""

    def __init__(self, stats, direction):
        self.stats = stats
        self.direction = direction
        self.buffer = b""

    def feed(self, data):
        self.buffer += data
        while len(self.buffer) >= 4:
            length = struct.unpack(">I", self.buffer[:4])[0]
            if len(self.buffer) < 4 + length:
                return
            body = self.buffer[4 : 4 + length]
            self.buffer = self.buffer[4 + length :]
            packet = body[0] if body else -1
            # DATA: the target or sender i32, then the game's kind.
            kind = body[5] if packet == 3 and len(body) > 5 else f"p{packet}"
            self.stats.note("tcp", self.direction, kind, 4 + length)


async def pump(reader, writer, impair, stats, direction):
    frames = Frames(stats, direction)
    loop = asyncio.get_running_loop()
    ready = loop.time()
    queue = asyncio.Queue()

    async def deliver():
        while True:
            when, data = await queue.get()
            if data is None:
                break
            wait = when - loop.time()
            if wait > 0:
                await asyncio.sleep(wait)
            writer.write(data)
            await writer.drain()
        writer.close()

    task = loop.create_task(deliver())
    while True:
        data = await reader.read(65536)
        if not data:
            break
        frames.feed(data)
        when = loop.time() + impair.delay()
        if impair.random.random() < impair.loss:
            stats.lost[f"tcp/{direction}"] += 1
            when += max(0.3, 2 * impair.latency)
        ready = max(ready, when)
        queue.put_nowait((ready, data))
    queue.put_nowait((ready, None))
    await task


async def serve(args):
    host, port = args.listen.rsplit(":", 1)
    server_host, server_port = args.server.rsplit(":", 1)
    target = (server_host, int(server_port))
    impair = Impairment(args)
    stats = Stats()
    loop = asyncio.get_running_loop()

    async def tcp(reader, writer):
        writer.get_extra_info("socket").setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        up_reader, up_writer = await asyncio.open_connection(*target)
        up_writer.get_extra_info("socket").setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        await asyncio.gather(
            pump(reader, up_writer, impair, stats, "up"),
            pump(up_reader, writer, impair, stats, "down"),
            return_exceptions=True,
        )

    await asyncio.start_server(tcp, host, int(port))
    await loop.create_datagram_endpoint(
        lambda: Udp(loop, target, impair, stats), local_addr=(host, int(port))
    )
    stop = asyncio.Event()
    loop.add_signal_handler(signal.SIGTERM, stop.set)
    loop.add_signal_handler(signal.SIGINT, stop.set)

    def write():
        if args.stats:
            with open(args.stats + ".tmp", "w") as out:
                json.dump(stats.report(), out)
            import os

            os.replace(args.stats + ".tmp", args.stats)

    while not stop.is_set():
        try:
            await asyncio.wait_for(stop.wait(), 5)
        except asyncio.TimeoutError:
            pass
        write()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen", required=True)
    parser.add_argument("--server", required=True)
    parser.add_argument("--loss", type=float, default=0)
    parser.add_argument("--latency", type=float, default=0)
    parser.add_argument("--jitter", type=float, default=0)
    parser.add_argument("--reorder", type=float, default=0)
    parser.add_argument("--duplicate", type=float, default=0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--stats")
    asyncio.run(serve(parser.parse_args()))


if __name__ == "__main__":
    main()
