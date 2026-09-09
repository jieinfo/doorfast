import json
import socket
import subprocess
import unittest


class GvsPeerUdpInjectorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(["make", "peer-udp-inject"], check=True)

    def receive_scenario(self, scenario):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
            receiver.bind(("127.0.0.1", 18300))
            receiver.settimeout(2)
            result = subprocess.run(
                ["build/gvs-peer-udp-inject", "--scenario", scenario],
                check=True,
                capture_output=True,
                text=True,
            )
            packet, peer = receiver.recvfrom(2048)
        receipt = json.loads(result.stdout)
        self.assertEqual("127.0.0.1", peer[0])
        self.assertEqual(scenario, receipt["scenario"])
        self.assertEqual("127.0.0.1:18300", receipt["target"])
        self.assertEqual(len(packet), receipt["bytes"])
        self.assertEqual(b"GVSGVS\xa5\xa5\xa5\xa5", packet[:10])
        self.assertEqual(len(packet) - 42, int.from_bytes(packet[40:42], "little"))
        return packet

    def test_sync_reply_is_fixed_91_81_from_lower_indoor_peer(self):
        packet = self.receive_scenario("sync-reply")
        self.assertEqual(44, len(packet))
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 1)), packet[16:22])
        self.assertEqual(bytes((0x91, 0x81)), packet[38:40])
        self.assertEqual(bytes((7, 0)), packet[42:44])

    def test_local_call_is_fixed_03_01_from_door_station(self):
        packet = self.receive_scenario("call-local")
        self.assertEqual(42, len(packet))
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x32, 2, 1, 0, 1, 0)), packet[16:22])
        self.assertEqual(bytes((0x03, 0x01)), packet[38:40])

    def test_peer_online_is_fixed_07_81_from_lower_indoor_peer(self):
        packet = self.receive_scenario("peer-online")
        self.assertEqual(48, len(packet))
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 1)), packet[16:22])
        self.assertEqual(bytes((0x07, 0x81)), packet[38:40])
        self.assertEqual(bytes((0, 1, 0, 0, 0, 0)), packet[42:48])

    def test_peer_probe_is_fixed_07_01_from_lower_indoor_peer(self):
        packet = self.receive_scenario("peer-probe")
        self.assertEqual(44, len(packet))
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 1)), packet[16:22])
        self.assertEqual(bytes((0x07, 0x01)), packet[38:40])
        self.assertEqual(bytes((0x12, 0x34)), packet[42:44])

    def test_periodic_sync_carries_fixed_period_json(self):
        packet = self.receive_scenario("periodic-sync")
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 1)), packet[16:22])
        self.assertEqual(bytes((0x91, 0x03)), packet[38:40])
        self.assertEqual(bytes((7, 0)), packet[42:44])
        self.assertEqual(
            b'{"TYPE":"Period","COUNT":1,"INFO":['
            b'{"KEY":"sim_state","VALUE":"present"}]}',
            packet[44:],
        )

    def test_normal_update_carries_new_version_and_fixed_json(self):
        packet = self.receive_scenario("normal-update")
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 2)), packet[10:16])
        self.assertEqual(bytes((0x61, 2, 1, 1, 1, 1)), packet[16:22])
        self.assertEqual(bytes((0x91, 0x03)), packet[38:40])
        self.assertEqual(bytes((8, 0)), packet[42:44])
        self.assertEqual(
            b'{"TYPE":"Normal","COUNT":1,"INFO":['
            b'{"KEY":"sim_state","VALUE":"updated"}]}',
            packet[44:],
        )

    def test_arbitrary_scenario_is_rejected(self):
        result = subprocess.run(
            ["build/gvs-peer-udp-inject", "--scenario", "arbitrary"],
            capture_output=True,
            text=True,
        )
        self.assertEqual(2, result.returncode)
        self.assertIn("unknown scenario", result.stderr)


if __name__ == "__main__":
    unittest.main()
