# Music scanning and playback results

Music uses the shared `MusicPlayerService` and asynchronous `AudioService`. Accepting a
play request means a worker was started; playback completion comes from the audio state.
Neither result proves audible output, successful SD-card recovery, or persisted playback
preferences.

## Library and retry

Library state distinguishes an unscanned/scanning library, a ready library, a genuinely empty
library, missing FileService, unavailable storage, and directory read failure. Scanning still
prefers `/music` and then uses the existing shallow root fallback, accepting WAV and MP3 names.
A supported extension does not prove that the file is decodable.

FileService publishes directory entries only after `opendir`, `readdir`, all metadata reads and
`closedir` succeed. A failure discards partial and stale results. Music also rejects a failed
subdirectory scan and removes its old playable list; it cannot silently offer a partial library
as a successful refresh.

The Songs page offers **Refresh / Retry**. Its monitor worker performs storage reads outside
the LVGL callback. The UI reads status snapshots and rebuilds the list when the library revision
changes. A song selection carries that revision so an old row cannot select a different song
after a rescan. Worker-creation failure remains a visible, retryable error.

## Playback and errors

- Playback admission, storage failure, empty library, another app's audio ownership, decoder
  failure and output failure have separate messages. An asynchronous error takes precedence
  over sample-rate/progress display; a failed request is not automatically called “No tracks”.
- WAV parsing validates RIFF/chunk boundaries, PCM format and complete sample frames. Early EOF
  and read failures cannot turn into 100% completion. Only successfully written PCM advances
  playback progress.
- MP3 reads distinguish errors from end-of-input. Incomplete frames and decoder failures cannot
  claim completion. Standard ID3 and APE metadata are handled separately from audio frames;
  host tests use the pinned Helix decoder, not a fake successful decoder.
- Failed hardware resume cancels the paused worker and retains its error so a new request can
  retry. Stop and automatic next-track handling are coordinated; observing state does not start
  another track. Shutdown retains task-owned state until in-flight I/O exits.
- A rejected volume change restores the slider and displayed percentage from the shared output
  configuration. The configuration itself retains the existing volume-receipt boundary.

The service deliberately waits for in-flight storage/audio I/O during destruction. This avoids
freeing state underneath a task; it does not promise a bounded shutdown when a hardware driver
never returns. Playback preferences still use the existing Settings writes and are not a new
durable device-effect contract. No remote music or backlight MCP endpoint is introduced.

## Software verification and remaining gates

- [Directory tests](../tests/file_directory/README.md) use actual host directories with injected
  POSIX operation failures and exercise the same directory reader as FileService.
- [Audio tests](../tests/audio_playback_service/README.md) compile the production asynchronous
  audio service with real files and the managed Helix decoder; codec/RTOS boundaries are fakes.
- [Music UI tests](../tests/music_ui/README.md) compile MusicApp, its services and real LVGL.
  The software display and storage/codec fakes do not validate the BigSmart touch panel, SD card,
  speaker, physical resource pressure, or wake/Recorder audio-focus integration.

Exact source/build identity and dated results are recorded in
[release evidence](ota-release-readiness.md). Real SD removal/slow-card and low-memory runs,
physical music/Recorder/voice preemption and resume, and audible playback remain open in the
  [roadmap](roadmap.md). Photos, File Manager, Camera and Recorder still need physical SD,
  touch, audio and resource acceptance. Recorder's final header/flush/close errors and Camera's
  failed completion delivery are covered by the host software suites; those tests do not replace
  device-side recovery or acoustic validation.
