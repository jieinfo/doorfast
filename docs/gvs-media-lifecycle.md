# GVS media lifecycle isolation

Doorfast binds received and transmitted media to the current call generation.
The runtime clears buffered audio, incomplete video fragments, and temporary
latest-media files at daemon startup, at the start of a new call generation,
and when a call ends.

A state change within one generation, such as ringing to talking, does not
clear media. A call preemption changes the generation and reports both the old
ending and the new start in one synchronization step, so old media is discarded
before packets for the new caller are accepted. Hangup, timeout, network loss,
and daemon shutdown also clear the media state.

The temporary files are:

- `/tmp/doorfast-latest.jpg`
- `/tmp/doorfast-latest.wav`

The HTTP bridge therefore reports unavailable until the active generation has
produced a fresh snapshot. This prevents a Home Assistant client or browser
from treating the previous call's image or audio as current media.

This behavior follows the cleanup order identified in the vendor-delivered
`Snippet/010-talkback-business.md` static call path and the generation boundary
required by the project media design. Unit tests cover startup, ringing,
talking, preemption, ending, and repeated terminal-state synchronization. Real
device media release timing remains a field acceptance item.
