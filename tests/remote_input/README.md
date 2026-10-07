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

The 30 Linux/WSL scenarios cover explicit control enable, real text/key application,
pointer down/up ordering, revoked queued input, session-name reuse, separate
authorization grants after disable/enable, deferred navigation ownership,
pointer release without waiting for cleanup, stale cleanup against a new held
pointer, synchronous navigation cleanup, controller destruction, and delayed
ACK callbacks across peer replacement/destruction.
They also verify cancellation replies for page transitions, local touch and
disable; synchronous reentry from those replies; exactly-once navigation results
when disable races admitted execution; and cancellation of all 49 bounded pending
replies with C++ heap allocation disabled.

The 019 extension drives real LVGL pointer devices and button event callbacks,
rather than treating a RELEASED sample as proof that cancellation is harmless.
After a down was accepted, disable, stream revocation/cleanup, local takeover and
page transition reset the remote gesture without RELEASED/CLICKED. A cancellation
epoch survives rapid disable/enable and peer replacement; the controller delivers
a cancellation release before dequeuing a new down because LVGL reset alone does
not clear its previous input state. Normal remote up still clicks exactly once.
Tests cover both physical-publication/OnLocalTouch orders and repeated physical
polling, preserving the new local gesture and unrelated physical-only clicks.

A Linux link wrapper intercepts the production controller's mutex unlock once,
then disables/revokes between dequeuing up and its final admission. This does not
patch the production source. It proves that cancellation advances even when the
cached pointer was already changed to up; otherwise the fallback release clicks.
The expected sequence is INDEV_RESET followed by a fresh press/release, not a
late action from the cancelled gesture. Logs print only seq/kind and a fixed
action name for non-move input, never text content.

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
