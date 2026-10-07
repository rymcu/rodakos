# Remote input lease and deferred execution tests

This target compiles the production `RemoteInputController`, `StreamLease`,
`DisplayControlAckTracker`, `ApplyRemoteTextInput` and `TouchPointerState` with
managed cJSON and LVGL 9.3. It creates real LVGL textareas; navigation scheduling
and navigation side effects use host callbacks so a test can deliberately hold
and release the deferred operation.

```sh
cmake -S tests/remote_input -B /tmp/rodakos-remote-input -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/rodakos-remote-input
ctest --test-dir /tmp/rodakos-remote-input --output-on-failure
```

The 20 scenarios cover explicit control enable, real text/key application,
pointer down/up ordering, revoked queued input, session-name reuse, separate
authorization grants after disable/enable, deferred navigation ownership,
pointer release without waiting for cleanup, stale cleanup against a new held
pointer, synchronous navigation cleanup, controller destruction, and delayed
ACK callbacks across peer replacement/destruction.
They also verify cancellation replies for page transitions, local touch and
disable; synchronous reentry from those replies; exactly-once navigation results
when disable races admitted execution; and cancellation of all 49 bounded pending
replies with C++ heap allocation disabled.

The controller checks the captured stream lease and authorization grant at final
execution. Revocation is nonblocking: one operation admitted before revocation
may finish, but another queued or deferred action must obtain its own admission.
Container locks are released before real UI actions and reply callbacks. A
page/local-touch cancellation rejects queued input on its original reply; it does
not report application success. Fixed-capacity reply collection adds no heap
allocation to the cancellation path. An already admitted navigation retains its
actual result, while cancelled deferred navigation cannot reply a second time. A
controller owns its state exclusively; destruction revokes its grant and makes
deferred callbacks inert. Pointer cleanup compares the original lease object,
not its session string.

The ACK tracker allocates an opaque identity on each peer Start. Its production
reply factory holds only a weak tracker reference plus that identity. Sequence
numbers remain monotonic within a peer; an old identity cannot enqueue a reply
or pass the final pre-send check after replacement. This suite exercises the
actual tracker/reply factory, but not the full WebRTC transport or hardware.
