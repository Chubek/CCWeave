\page manual_os Operating System and Transports

# Operating System and Transports

## POSIX terminal lifecycle

The OS-facing TTyTk headers are deliberately explicit:

- `Winsize` reads and updates rows, columns, and pixel dimensions;
- `SignalTools` turns signals such as SIGWINCH into application events;
- `PsuedoTerm` wraps PTY creation, fork, read, write, and resize;
- `JobControl` manages process groups and foreground ownership;
- `FlowControl` handles throttling, backpressure, and XON/XOFF;
- `SerialPort` configures hardware terminals;
- `IPCTools` provides local process-to-process channels.

A full-screen application should establish terminal mode, install signal
handling, create or attach its input/output descriptors, and restore state on
all exit paths. Keep restoration separate from rendering so errors do not
leave the user's terminal in an alternate mode.

## Shells and PTYs

Use `PsuedoTerm` when the child must believe it is connected to a terminal.
Use TTyUtils `Shell` for command orchestration where captured pipes or
pipelines are sufficient. Use TTyUtils `Terminal` when the child output must
be interpreted and displayed as a terminal session.

`MuxTools` and `EmbedTools` compose PTYs into multiplexed or nested
applications. They should remain at the boundary: the screen model still
belongs to TTyTk and the application policy belongs to TTyUtils.

## Network and local communication

TTyTk supplies synchronous primitives for server and peer-to-peer building
blocks through `ServerNetUtils` and `P2PNetUtils`. TTyUtils `NetLink` adds a
non-blocking transport abstraction integrated with `EventLoop`; `SSH`,
`Telnet`, `Serial`, and `P2P` build protocol policy on that abstraction.

The higher-level remote stack is:

```text
NetLink
  ├── SSH / Telnet / Serial
  ├── P2P
  └── Roam / Multiplex / SessionShare
```

Treat authentication, host identity, and remote clipboard access as explicit
policy. The transport layer should report errors and state transitions rather
than silently retrying unsafe operations.

## Event loop boundary

TTyTk operations are synchronous and non-blocking. TTyUtils owns coroutine
awaitables and scheduling in `EventLoop`. A TTyTk caller can integrate with a
different scheduler by polling descriptors and passing completed input to the
same parser or state object.
