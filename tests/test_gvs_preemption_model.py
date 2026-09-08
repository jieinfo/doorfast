"""Offline specification tests, not device interoperability tests."""
import unittest
from gvs_preemption_model import Model


class PreemptionTests(unittest.TestCase):
    def setUp(self):
        self.model = Model()
        self.model.call('A', 4)

    def test_duplicate_preserves_generation_and_pick(self):
        self.model.pick_generation = self.model.generation
        before = self.model.snapshot()
        self.assertEqual(self.model.call('A', 4), 'duplicate')
        self.assertEqual(self.model.snapshot(), before)

    def test_equal_priority_does_not_replace(self):
        before = self.model.snapshot()
        self.assertEqual(self.model.call('B', 4), 'rejected')
        self.assertEqual(self.model.snapshot(), before)

    def test_preemption_orders_events_and_clears_pick(self):
        old = self.model.generation
        self.model.pick_generation = old
        self.assertEqual(self.model.call('B', 3), 'preempted')
        self.assertGreater(self.model.generation, old)
        self.assertEqual(self.model.pick_generation, 0)
        self.assertEqual(self.model.events[-2:], [('ended', 'A'), ('incoming', 'B')])

    def test_old_peer_end_cannot_end_replacement(self):
        self.model.call('B', 3)
        before = self.model.snapshot()
        self.assertEqual(self.model.end('A'), 'ignored')
        self.assertEqual(self.model.snapshot(), before)

    def test_old_local_timer_cannot_end_replacement(self):
        old = self.model.generation
        self.model.call('B', 3)
        self.assertFalse(self.model.timeout(old))
        self.assertEqual(self.model.state, 'ringing')
        self.assertTrue(self.model.timeout(self.model.generation))

    def test_unknown_category_preserves_state(self):
        before = self.model.snapshot()
        self.assertEqual(self.model.call('B', -1), 'unsupported')
        self.assertEqual(self.model.snapshot(), before)

    def test_same_peer_wire_end_is_explicitly_ambiguous(self):
        self.model.end('A')
        self.model.call('A', 4)
        before = self.model.snapshot()
        self.assertEqual(self.model.end('A'), 'ambiguous')
        self.assertEqual(self.model.snapshot(), before)

    def test_all_valid_priority_pairs(self):
        # Independent expected table from static comparison, not computed ranks.
        winners = {0: {3, 4}, 1: {3, 4}, 7: {3, 4}, 3: {4}, 4: set()}
        for incoming in winners:
            for current in winners:
                with self.subTest(incoming=incoming, current=current):
                    model = Model()
                    model.call('old', current)
                    result = model.call('new', incoming)
                    self.assertEqual(result, 'preempted' if current in winners[incoming] else 'rejected')


if __name__ == '__main__':
    unittest.main()
