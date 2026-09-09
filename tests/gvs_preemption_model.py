"""Test-only abstract model. No packets, credentials, sockets or production imports."""


class Model:
    def __init__(self):
        self.peer = None
        self.category = None
        self.state = 'idle'
        self.generation = 0
        self.pick_generation = 0
        self.events = []
        self.seen = set()
        self.repeated_peer = False

    def snapshot(self):
        return (self.peer, self.category, self.state, self.generation,
                self.pick_generation, tuple(self.events), frozenset(self.seen),
                self.repeated_peer)

    def call(self, peer, category):
        ranks = {0: 2, 1: 2, 7: 2, 3: 1, 4: 0}
        if category not in ranks:
            return 'unsupported'
        replacing = self.state == 'ringing'
        if replacing:
            if peer == self.peer:
                return 'duplicate'
            if ranks[category] <= ranks[self.category]:
                return 'rejected'
            self.events.append(('ended', self.peer))
        self.repeated_peer = peer in self.seen
        self.seen.add(peer)
        self.peer, self.category = peer, category
        self.state = 'ringing'
        self.generation += 1
        self.pick_generation = 0
        self.events.append(('incoming', peer))
        return 'preempted' if replacing else 'started'

    def end(self, peer):
        if self.state != 'ringing' or peer != self.peer:
            return 'ignored'
        if self.repeated_peer:
            # Conservative research result, NOT a vendor receive policy.
            return 'ambiguous'
        self.state = 'ended'
        self.pick_generation = 0
        self.events.append(('ended', peer))
        return 'ended'

    def timeout(self, generation):
        if self.state != 'ringing' or generation != self.generation:
            return False
        self.state = 'ended'
        self.pick_generation = 0
        self.events.append(('ended', self.peer))
        return True
