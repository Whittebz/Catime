# Week Planner Calendar fork changes

This fork retains Catime's Apache-2.0 license and upstream attribution.

## 1.5.0-wpc.2

- Multi-vault support: the named pipe now accepts up to 8 concurrent clients
  (one per Obsidian vault). Events are delivered per client and only removed
  once every connected client acknowledges them, so a second vault connected
  mid-session never misses a completion.
- Click-to-start focus: starting a countdown (right-click menu, quick times,
  hotkeys) now generates a local session ID, starts the IPC session, and pushes
  the started snapshot to Obsidian; an active session is replaced with
  `user_replaced` first. "End Focus" finishes the current session and pushes
  the completed record.
- Double-clicking the time display pauses/resumes an active countdown (routed
  through the existing tray pause path, which already notifies clients).
- Offline backfill: terminal sessions are now persisted in an unacknowledged
  history (up to 64 records) and replayed to the next connecting plugin, so
  sessions completed while Obsidian is closed — including full Pomodoro plans
  across a Catime restart — are reconciled on reconnect.
- Added platform-independent tests for the terminal-session history and the
  per-client multi-client delivery queue.

## 1.5.0-wpc.1

- Reserve the local named-pipe protocol used by Week Planner Calendar.
- Define stable IPC states, causes, errors, bounds, and golden test vectors.
- Add a current-user Windows named-pipe service, bounded JSON request parser,
  UI-thread command bridge, revision-based replay protection, and durable
  external-session recovery.
- Add a persisted phase queue so focus and break sequences continue while the
  Obsidian client is disconnected.
- Add platform-independent protocol, parser, state-machine, and queued-phase
  tests.
- Keep Catime's upstream semantic version separate from the fork distribution
  version so existing update and configuration migration behavior is preserved.

Upstream project: https://github.com/vladelaina/Catime
