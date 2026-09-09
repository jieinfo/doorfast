#!/usr/bin/env python3
"""Offline sensitivity experiment: insert synthetic call requests in a private PCAP.

Never modifies input or transmits traffic. Output exists only in a temporary directory.
This is not evidence that the inserted bytes equal the missing original packets.
"""
import argparse
import collections
import hashlib
import json
import pathlib
import struct
import subprocess
import tempfile


def records(data):
    if data[:4] != b'\xd4\xc3\xb2\xa1':
        raise ValueError('requires little-endian microsecond classic PCAP')
    if struct.unpack_from('<I', data, 20)[0] != 1:
        raise ValueError('requires Ethernet PCAP')
    offset = 24
    while offset < len(data):
        if offset + 16 > len(data):
            raise ValueError('truncated record header')
        sec, usec, caplen, wirelen = struct.unpack_from('<IIII', data, offset)
        offset += 16
        if offset + caplen > len(data):
            raise ValueError('truncated capture record')
        yield sec * 1000000 + usec, wirelen, data[offset:offset + caplen]
        offset += caplen


def control(packet):
    # Unsupported encapsulations are counted separately, never silently inferred.
    if len(packet) < 42 or packet[12:14] != b'\x08\x00':
        return None
    ihl = (packet[14] & 15) * 4
    if ihl < 20 or packet[23] != 17 or len(packet) < 14 + ihl + 8:
        return None
    if int.from_bytes(packet[20:22], 'big') & 0x3fff:
        return None
    udp = 14 + ihl
    src, dst, length = struct.unpack_from('!HHH', packet, udp)
    if 8300 not in (src, dst) or length < 50 or udp + length > len(packet):
        return None
    gvs = packet[udp + 8:udp + length]
    if gvs[:10] != b'GVSGVS\xa5\xa5\xa5\xa5':
        return None
    return gvs


def checksum(data):
    data += b'\0' * (len(data) % 2)
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def synthetic_call(reply):
    # All transport addresses are synthetic. Logical endpoints are reversed from
    # the observed reply only to test the existing session correlation.
    payload = bytes.fromhex('00 20 6f 00 00 00 1e 00 01 00 00 00 00 00 00')
    gvs = (b'GVSGVS\xa5\xa5\xa5\xa5' + reply[16:22] + reply[10:16]
           + bytes(16) + b'\x03\x01\x0f\x00' + payload)
    udp = struct.pack('!HHHH', 8300, 8300, 8 + len(gvs), 0) + gvs
    ip = bytearray(bytes.fromhex('450000000000400040110000c0000201c0000202'))
    struct.pack_into('!H', ip, 2, 20 + len(udp))
    struct.pack_into('!H', ip, 10, checksum(bytes(ip)))
    return bytes.fromhex('0200000000020200000000010800') + bytes(ip) + udp


def write_record(out, timestamp, wirelen, packet):
    sec, usec = divmod(timestamp, 1000000)
    out.write(struct.pack('<IIII', sec, usec, len(packet), wirelen))
    out.write(packet)


def run(binary, path, identity):
    output = subprocess.check_output([str(binary), '--inspect-pcap', str(path), identity], text=True)
    return {k: int(v) for k, v in (item.split('=') for item in output.split())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=pathlib.Path)
    parser.add_argument('--binary', type=pathlib.Path, default=pathlib.Path('build/doorfast'))
    parser.add_argument('--identity', required=True)
    args = parser.parse_args()
    data = args.capture.read_bytes()
    captured = list(records(data))
    histogram = collections.Counter()
    injected = 0
    last_reply = {}
    with tempfile.TemporaryDirectory(prefix='doorfast-hybrid-') as temporary:
        path = pathlib.Path(temporary) / 'hybrid.pcap'
        with path.open('wb') as out:
            out.write(data[:24])
            for timestamp, wirelen, packet in captured:
                gvs = control(packet)
                if gvs:
                    histogram[f'{gvs[38]:02x}/{gvs[39]:02x}'] += 1
                    key = gvs[10:22]
                    if gvs[38:40] == b'\x03\x81':
                        # >3 s gap is an explicit experiment heuristic, not protocol truth.
                        if key not in last_reply or timestamp - last_reply[key] > 3000000:
                            synthetic = synthetic_call(gvs)
                            write_record(out, timestamp, len(synthetic), synthetic)
                            injected += 1
                        last_reply[key] = timestamp
                write_record(out, timestamp, wirelen, packet)
        report = dict(source_sha256=hashlib.sha256(data).hexdigest(),
                      observed_operations=dict(sorted(histogram.items())),
                      synthetic_initial_requests=injected,
                      original=run(args.binary.resolve(), args.capture, args.identity),
                      hybrid=run(args.binary.resolve(), path, args.identity),
                      limitation='Synthetic initial requests; no proof of missing packet bytes or device interoperability')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
