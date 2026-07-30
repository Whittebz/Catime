# Week Planner Calendar fork changes

This fork retains Catime's Apache-2.0 license and upstream attribution.

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
