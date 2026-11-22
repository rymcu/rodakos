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

## Music app UI

The player follows the approved 320x240 design. A 96 px media-colored cover sits beside the song
name and, below it, the artist; both come from a 「歌名-歌手」 file name, and a name without a dash
is shown as the song only. Under the names three chips set the playback mode (shuffle and
repeat-one turn the accent color, repeat-one adds a small 「1」), open the volume sheet and show the
current volume, and Stop. A read-only progress bar shows elapsed and total time. The transport
row is previous, a solid circular play key, and next. While a track loads or plays, a crossed
microphone in the header shows that wake listening is paused; it disappears on pause.

`AudioPlaybackState` reports `position_ms` and `duration_ms`. WAV time is exact. MP3 length comes
from the Xing/Info frame count in the first frame when present, otherwise from the first frame's
bitrate, so variable-bitrate files without that tag show an estimate.

Errors appear in the error color on the artist line and turn the play key into a retry icon.
Playback retry resolves the failed track by path and checks the library revision before playing
it. A request rejected while a track still plays or is paused only shows the message; the play
key keeps its pause/play meaning so the next tap cannot restart the song. An empty library, a
missing card and a read failure use the same layout with a grey cover, and the play key rescans.

The Songs page title shows the track count. Rows show the number, song and artist; the current
track uses the accent color and a play icon in place of its number. Seven row slots are rebound
while scrolling, so the object count does not grow with the library. It takes a track snapshot
on open and when the library revision changes, and releases it on close. A rebind or revision
change cancels a held selection. Refresh is disabled while the monitor worker scans outside LVGL.
Library failures show the reason and 「重试」, including when the worker cannot start.

The volume control is a bottom sheet over the dimmed player, created when opened and deleted when
closed. It confirms changes through the shared service and restores its last confirmed value if a
write fails. Tapping outside the sheet, header back or system back closes it without changing
playback.

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
- A single MP3 frame that Helix rejects (for example `ERR_MP3_INVALID_SCALEFACT` from a bad
  bit-reservoir claim) is dropped, about 26 ms of audio, and playback continues. More than eight
  consecutive bad frames still end the track with "MP3 decode failed". The MP3 PCM staging buffer
  prefers PSRAM, because internal RAM is fragmented once wake listening is resident.
- Failed hardware resume cancels the paused worker and retains its error so a new request can
  retry. Stop and automatic next-track handling are coordinated; observing state does not start
  another track. Shutdown retains task-owned state until in-flight I/O exits.
- A rejected volume change restores the slider and displayed percentage from the shared output
  configuration. The configuration itself retains the existing volume-receipt boundary.

## Shared I2S bus and wake listening

ES8311 playback and ES7210 capture share one I2S clock. Always-on wake capture holds it at 16 kHz,
and esp_codec_dev refuses a playback open at any other rate (`conflict sample_rate`), which surfaced
as "Codec open failed" for 44.1 kHz songs. Music therefore takes the bus instead of resampling:

- Every playback output open first calls `VoiceWakeService::ReleaseInputForMedia()`, which closes
  the wake microphone synchronously.
- While a track is loading or playing, or the playback output is open, the wake supervisor keeps
  wake listening paused. "你好达克" is not available during music.
- Pausing closes the playback output at the worker's next pause poll; Resume reopens it with the
  retained format and continues from the paused position. Completion, stop and errors also close
  it. Wake listening re-arms on the next supervisor tick, about one second later.

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
physical music/Recorder/voice preemption, and audible playback remain open in the
  [roadmap](roadmap.md). On 2026-11-21 a BigSmart device played 44.1 kHz stereo MP3s through
  pause, wake detection while paused, and resume from the paused position. Photos, File Manager, Camera and Recorder still need physical SD,
  touch, audio and resource acceptance. Recorder's final header/flush/close errors and Camera's
  failed completion delivery are covered by the host software suites; those tests do not replace
  device-side recovery or acoustic validation.
