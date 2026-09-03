#!/usr/bin/env python3
"""采集 RodakOS 语音助手的实机长稳串口日志并输出 JSON 摘要。

这个工具只负责观测，不会刷写固件、发送复位命令或重连设备。串口只打开一次，
默认将 DTR/RTS 都保持为 False，以免 USB-UART 的线路变化触发板子复位。
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import json
import math
from pathlib import Path
import re
import sys
import time
from typing import Any, Iterable

try:
    import serial
except ImportError:  # 让 --help 在未安装 pyserial 时仍可用
    serial = None  # type: ignore[assignment]


DEFAULT_PORT = "COM3"
DEFAULT_BAUD = 115200
DEFAULT_DURATION_SECONDS = 30 * 60
SERIAL_READ_TIMEOUT_SECONDS = 0.25

_ANSI_ESCAPE_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
_LOG_START_RE = re.compile(
    r"(?<![A-Za-z0-9_])(?P<level>[IWEVD])\s*\((?P<timestamp>\d+)\)\s+"
    r"(?P<tag>[A-Za-z][A-Za-z0-9_.-]*)\s*:\s*"
)
_KEY_VALUE_RE = re.compile(
    r"(?P<key>[A-Za-z][A-Za-z0-9_]*)=(?P<value>-?(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d+)?))"
)
_REARM_RE = re.compile(r"\bre[- ]?arm(?:ed|ing)?\b", re.IGNORECASE)

_HEALTH_KEY_NAMES = {
    "free",
    "largest",
    "internal_free",
    "internal_min",
    "internal_largest",
    "psram_free",
    "psram_min",
    "psram_largest",
    "free_heap",
    "minimum_free_heap_size",
    "sram_free",
    "sram_largest",
    "heap_free",
    "heap_largest",
    "stack_min_free",
    "stack_high_water_mark",
}

# 这些模式针对 ESP-IDF、FreeRTOS 和 RodakOS 当前常见的故障输出；解析时仍保留原始行。
_FAILURE_RULES: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "stack_overflow",
        re.compile(r"\bstack[\s_-]*overflow\b|stack overflow in task", re.IGNORECASE),
    ),
    (
        "watchdog",
        re.compile(r"\b(?:task\s+)?watchdog\b|\b(?:twdt|wdt)\b", re.IGNORECASE),
    ),
    (
        "assert",
        re.compile(
            r"\bassert(?:ion)?\s*(?:failed|failure|error|:|\()|abort\(\)\s+was\s+called",
            re.IGNORECASE,
        ),
    ),
    (
        "panic",
        re.compile(
            r"guru meditation|\bpanic(?:'ed|ed)?\b|\bbacktrace\b|abort\(\)\s+was\s+called",
            re.IGNORECASE,
        ),
    ),
    (
        "out_of_memory",
        re.compile(
            r"out\s+of\s+memory|no\s*mem|esp_err_no_mem|failed to (?:alloc|allocate)\b|"
            r"failed to create .*task|task creation failed|task unavailable:.*(?:free|largest)|"
            r"heap_caps_.*\b(?:fail|error)\b",
            re.IGNORECASE,
        ),
    ),
    (
        "heap_corruption",
        re.compile(
            r"corrupt(?:ed)? heap|heap corruption|multi_heap_assert|bad head|"
            r"stack smashing|invalid heap block",
            re.IGNORECASE,
        ),
    ),
    (
        "fatal_exception",
        re.compile(
            r"fatal exception|unhandled debug exception|loadprohibited|storeprohibited|"
            r"illegalinstruction|instrfetchprohibited",
            re.IGNORECASE,
        ),
    ),
    (
        "transport",
        re.compile(
            r"websocket(?:\s+start)?\s+failed|voice websocket.*(?:failed|error)|"
            r"assistant start failed|voice cloud.*(?:failed|error)",
            re.IGNORECASE,
        ),
    ),
    (
        "reset",
        re.compile(r"\brebooting\.\.\.|\brst:\s*|brownout detector", re.IGNORECASE),
    ),
)


@dataclass(frozen=True)
class LogRecord:
    """一条可识别的 ESP-IDF 日志记录。"""

    index: int
    level: str | None
    timestamp_ms: int | None
    tag: str | None
    message: str


def _clean_text(value: str) -> str:
    return _ANSI_ESCAPE_RE.sub("", value)


def _single_line(value: str) -> str:
    return " ".join(value.split())


def _iter_records(value: str | bytes) -> list[LogRecord]:
    """解析带或不带换行的 ESP-IDF 日志，兼容开头残留的二进制字节。"""

    text = value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value
    text = _clean_text(text)
    matches = list(_LOG_START_RE.finditer(text))
    records: list[LogRecord] = []
    for index, match in enumerate(matches):
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        message = text[match.end() : end].strip(" \r\n")
        records.append(
            LogRecord(
                index=index,
                level=match.group("level"),
                timestamp_ms=int(match.group("timestamp")),
                tag=match.group("tag"),
                message=message,
            )
        )
    return records


def _record_event(record: LogRecord, **extra: Any) -> dict[str, Any]:
    event: dict[str, Any] = {
        "timestamp_ms": record.timestamp_ms,
        "tag": record.tag,
        "level": record.level,
        "message": _single_line(record.message),
    }
    event.update(extra)
    return event


def _records_near(left: LogRecord, right: LogRecord) -> bool:
    """判断两条记录是否很可能是同一事件的不同组件日志。"""

    index_distance = abs(left.index - right.index)
    if left.timestamp_ms is not None and right.timestamp_ms is not None:
        return index_distance <= 20 and abs(left.timestamp_ms - right.timestamp_ms) <= 500
    return index_distance <= 5


def _extract_wake_word(message: str) -> str:
    match = re.search(r"wake\s+word\s+detected\s*:?[ \t]*(.*)", message, re.IGNORECASE)
    if match is None:
        return "wake word"
    word = _single_line(match.group(1)).strip(" :\t")
    return word or "wake word"


def _normalise_word(word: str) -> str:
    return re.sub(r"[^\w\u4e00-\u9fff]+", "", word.casefold())


def _wake_events(records: Iterable[LogRecord]) -> list[dict[str, Any]]:
    candidates: list[tuple[LogRecord, str]] = []
    for record in records:
        message = record.message
        lowered = message.lower()
        if "wake word detected" not in lowered:
            continue
        tag = (record.tag or "").lower()
        if "wake" not in tag and "wake word" not in lowered:
            continue
        candidates.append((record, _extract_wake_word(message)))

    events: list[dict[str, Any]] = []
    source_records: list[list[LogRecord]] = []
    for record, word in candidates:
        normalised = _normalise_word(word)
        merged = False
        for event_index in range(len(events) - 1, -1, -1):
            previous_records = source_records[event_index]
            previous_word = str(events[event_index]["word"])
            if _normalise_word(previous_word) != normalised:
                continue
            if not _records_near(previous_records[-1], record):
                continue
            event = events[event_index]
            event["sources"] = sorted(set([*event["sources"], record.tag]))
            event["lines"] = [*event["lines"], _single_line(record.message)]
            source_records[event_index].append(record)
            merged = True
            break
        if merged:
            continue
        events.append(
            {
                "timestamp_ms": record.timestamp_ms,
                "word": word,
                "sources": [record.tag],
                "lines": [_single_line(record.message)],
            }
        )
        source_records.append([record])
    return events


def _interaction_events(records: Iterable[LogRecord], phrase: str) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    seen: set[tuple[int | None, str, str]] = set()
    for record in records:
        lowered = record.message.lower()
        if phrase not in lowered:
            continue
        tag = (record.tag or "").lower()
        if "voice" not in tag and "interaction" not in lowered:
            continue
        key = (record.timestamp_ms, tag, _single_line(record.message).casefold())
        if key in seen:
            continue
        seen.add(key)
        fields: dict[str, Any] = {}
        for key_name in ("trigger", "focus_token", "generation", "session"):
            match = re.search(
                rf"\b{re.escape(key_name)}=([^\s,]+)", record.message, re.IGNORECASE
            )
            if match is None:
                continue
            raw_value = match.group(1)
            number = _number(raw_value)
            fields[key_name] = number if number is not None else raw_value
        events.append(_record_event(record, **fields))
    return events


def _assistant_listening_events(records: Iterable[LogRecord]) -> list[dict[str, Any]]:
    candidates: list[LogRecord] = []
    for record in records:
        lowered = record.message.lower()
        tag = (record.tag or "").lower()
        is_listening = (
            "sent listen:start" in lowered
            or ("voiceassistant" in tag and lowered.startswith("listening"))
        )
        if is_listening:
            candidates.append(record)
    return [_record_event(record) for record in _dedupe_records(candidates)]


def _wake_arm_candidates(records: Iterable[LogRecord]) -> tuple[list[LogRecord], list[LogRecord]]:
    armed: list[LogRecord] = []
    explicit_rearm: list[LogRecord] = []
    for record in records:
        lowered = record.message.lower()
        tag = (record.tag or "").lower()
        if _REARM_RE.search(lowered) and ("wake" in tag or "wake" in lowered):
            explicit_rearm.append(record)

        is_armed = (
            "always-on wake monitoring armed" in lowered
            or ("wake monitoring" in lowered and "armed" in lowered)
            or ("wake monitoring" in lowered and "started" in lowered)
            or ("voice wake service enabled" in lowered and "listening" in lowered)
            or ("wake listener" in lowered and "listening" in lowered and "enabled" in lowered)
        )
        if is_armed and ("wake" in tag or "wake" in lowered or "assistant" in lowered):
            armed.append(record)
    return armed, explicit_rearm


def _dedupe_records(records: list[LogRecord]) -> list[LogRecord]:
    result: list[LogRecord] = []
    for record in records:
        if result and _records_near(result[-1], record):
            continue
        result.append(record)
    return result


def _wake_arm_events(records: Iterable[LogRecord]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    record_list = list(records)
    armed_records, explicit_records = _wake_arm_candidates(record_list)
    armed_records = _dedupe_records(armed_records)
    explicit_records = _dedupe_records(explicit_records)

    listening = [_record_event(record, kind="armed") for record in armed_records]
    rearm_with_index: list[tuple[int, dict[str, Any]]] = []
    interaction_stops = [
        record for record in record_list if "interaction stopped" in record.message.lower()
    ]

    # 若采集从一次交互中途开始，首条 armed 也可能是一次实际重新布防；因此同时参考
    # 记录顺序和此前是否出现过 interaction stopped，而不是无条件忽略第一条。
    for index, record in enumerate(armed_records):
        follows_stop = any(stopped.index < record.index for stopped in interaction_stops)
        if index > 0 or follows_stop:
            rearm_with_index.append(
                (record.index, _record_event(record, kind="armed", derived=True))
            )

    # 某些固件版本只打印 "re-arming" 而没有再次打印 armed，补充这类事件；
    # 若两者相邻则视为同一次，避免计数翻倍。
    for record in explicit_records:
        derived_armed = [
            armed
            for index, armed in enumerate(armed_records)
            if index > 0 or any(stopped.index < armed.index for stopped in interaction_stops)
        ]
        if any(_records_near(record, armed) for armed in derived_armed):
            continue
        rearm_with_index.append((record.index, _record_event(record, kind="explicit")))
    rearm_with_index.sort(key=lambda item: item[0])
    rearm = [event for _, event in rearm_with_index]
    return listening, rearm


def _number(value: str) -> int | float | None:
    try:
        if value.lower().startswith("0x"):
            return int(value, 16)
        parsed = float(value)
    except ValueError:
        return None
    if not math.isfinite(parsed):
        return None
    return int(parsed) if parsed.is_integer() else parsed


def _key_values(message: str) -> dict[str, int | float | str]:
    values: dict[str, int | float | str] = {}
    for match in _KEY_VALUE_RE.finditer(message):
        key = match.group("key").lower()
        number = _number(match.group("value"))
        values[key] = number if number is not None else match.group("value")
    return values


def _voice_health(records: Iterable[LogRecord]) -> dict[str, Any]:
    snapshots: list[dict[str, Any]] = []
    for record in records:
        tag = (record.tag or "").lower()
        if "voice" not in tag:
            continue
        values = _key_values(record.message)
        metrics: dict[str, int | float] = {}
        for key, value in values.items():
            if key not in _HEALTH_KEY_NAMES and not any(
                token in key for token in ("free", "largest", "stack", "heap")
            ):
                continue
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                metrics[key] = value
        if not metrics:
            continue
        if "voice health:" in record.message.lower() or "interaction memory" in record.message.lower():
            metrics = {
                key: value
                for key, value in values.items()
                if isinstance(value, (int, float)) and not isinstance(value, bool)
            }
        snapshot: dict[str, Any] = {
            "timestamp_ms": record.timestamp_ms,
            "tag": record.tag,
            "level": record.level,
            "message": _single_line(record.message),
            "metrics": metrics,
        }
        # 同时提供扁平字段，便于 PowerShell/jq 直接筛选。
        snapshot.update(metrics)
        snapshots.append(snapshot)

    minimum: dict[str, int | float] = {}
    maximum: dict[str, int | float] = {}
    for snapshot in snapshots:
        for key, value in snapshot["metrics"].items():
            if key not in minimum or value < minimum[key]:
                minimum[key] = value
            if key not in maximum or value > maximum[key]:
                maximum[key] = value
    return {
        "count": len(snapshots),
        "snapshots": snapshots,
        "latest": snapshots[-1] if snapshots else None,
        "minimum": minimum,
        "maximum": maximum,
    }


def _failure_markers(text: str, records: Iterable[LogRecord]) -> tuple[list[dict[str, Any]], dict[str, int]]:
    del records  # 保留参数以便调用方可以传入已解析记录；行级扫描能覆盖无头部的 panic 输出。
    clean = _clean_text(text)
    markers: list[dict[str, Any]] = []
    counts = {name: 0 for name, _ in _FAILURE_RULES}
    counts["error"] = 0
    seen: set[tuple[str, int | None, str]] = set()

    for raw_line in clean.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        header = _LOG_START_RE.search(line)
        level: str | None = None
        timestamp_ms: int | None = None
        tag: str | None = None
        payload = line
        if header is not None:
            level = header.group("level")
            timestamp_ms = int(header.group("timestamp"))
            tag = header.group("tag")
            payload = line[header.end() :].strip()
        matched = [name for name, pattern in _FAILURE_RULES if pattern.search(payload)]
        if level == "E" and not matched:
            matched = ["error"]
        for name in matched:
            key = (name, timestamp_ms, payload)
            if key in seen:
                continue
            seen.add(key)
            counts[name] += 1
            markers.append(
                {
                    "kind": name,
                    "timestamp_ms": timestamp_ms,
                    "tag": tag,
                    "level": level,
                    "message": payload,
                }
            )
    return markers, counts


def parse_log(value: str | bytes) -> dict[str, Any]:
    """解析串口日志，返回不含采集元数据的语音稳定性摘要。"""

    text = value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value
    records = _iter_records(text)
    wakes = _wake_events(records)
    started = _interaction_events(records, "interaction started")
    stopped = _interaction_events(records, "interaction stopped")
    listening = _assistant_listening_events(records)
    wake_armed, rearm = _wake_arm_events(records)
    health = _voice_health(records)
    failure_markers, failure_counts = _failure_markers(text, records)
    failure_flags = {name: count > 0 for name, count in failure_counts.items()}
    # 通用 E 级日志（例如 MQTT 短暂断线）保留在摘要中，但不应阻断语音长稳门禁。
    # 只有已归类的语音/系统致命标记才让采集命令返回失败；reset 另行统计，
    # 因为串口开始前可能残留一次正常启动复位行。
    fatal_failure = any(
        count > 0 for name, count in failure_counts.items() if name not in {"reset", "error"}
    )

    events = {
        "wake_detected": len(wakes),
        "interaction_started": len(started),
        "interaction_stopped": len(stopped),
        "listening": len(listening),
        "wake_armed": len(wake_armed),
        "rearm": len(rearm),
    }
    return {
        "records": len(records),
        "events": events,
        # 顶层计数是刻意保留的，便于不支持嵌套 JSON 查询的串口脚本使用。
        **events,
        "wake_events": wakes,
        "interaction_start_events": started,
        "interaction_stop_events": stopped,
        "listening_events": listening,
        "wake_armed_events": wake_armed,
        "rearm_events": rearm,
        "voice_health": health,
        "voice_health_snapshots": health["snapshots"],
        "failure": fatal_failure,
        "has_failure_markers": bool(failure_markers),
        "failure_flags": failure_flags,
        "failure_counts": failure_counts,
        "failure_markers": failure_markers,
    }


def _positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("必须是正数") from error
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("必须是有限的正数")
    return parsed


def _positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("必须是正整数") from error
    if parsed <= 0:
        raise argparse.ArgumentTypeError("必须是正整数")
    return parsed


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="单会话采集 RodakOS 语音助手串口日志（默认 30 分钟，不主动复位）。",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="串口名，例如 COM3")
    parser.add_argument("--baud", type=_positive_int, default=DEFAULT_BAUD, help="串口波特率")
    parser.add_argument(
        "--duration",
        type=_positive_float,
        default=float(DEFAULT_DURATION_SECONDS),
        metavar="SECONDS",
        help="采集时长（秒），可设为 1800 或更长",
    )
    parser.add_argument(
        "--log",
        type=Path,
        default=None,
        help="原样串口日志路径；省略时写入 build/logs/voice-stability-时间戳.log",
    )
    return parser.parse_args(argv)


def _default_log_path() -> Path:
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    return Path("build") / "logs" / f"voice-stability-{stamp}.log"


def _write_stdout(data: bytes) -> None:
    """原样转发串口字节；被管道关闭时不影响磁盘日志。"""

    try:
        output = getattr(sys.stdout, "buffer", None)
        if output is not None:
            output.write(data)
            output.flush()
        else:
            sys.stdout.write(data.decode("utf-8", errors="replace"))
            sys.stdout.flush()
    except (BrokenPipeError, OSError):
        pass


def _set_line_state(port: Any) -> None:
    # 属性在 open 前后各设置一次，覆盖 pyserial 后端的默认线路状态。
    try:
        port.dtr = False
    except (AttributeError, OSError, ValueError):
        pass
    try:
        port.rts = False
    except (AttributeError, OSError, ValueError):
        pass


def capture_serial(args: argparse.Namespace, log_path: Path) -> dict[str, Any]:
    """采集一次串口会话并返回采集元数据；不会尝试自动重连。"""

    started_at = datetime.now().astimezone()
    started_monotonic = time.monotonic()
    captured_bytes = 0
    opened = False
    interrupted = False
    serial_error: str | None = None
    port: Any = None

    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("wb") as log_file:
        try:
            if serial is None:
                raise RuntimeError("未安装 pyserial，请先运行 python -m pip install pyserial")
            port = serial.serial_for_url(
                args.port,
                baudrate=args.baud,
                timeout=SERIAL_READ_TIMEOUT_SECONDS,
                do_not_open=True,
            )
            _set_line_state(port)
            if not getattr(port, "is_open", False):
                port.open()
            opened = True
            _set_line_state(port)
            deadline = time.monotonic() + args.duration
            print(
                f"开始语音长稳采集：port={args.port} baud={args.baud} "
                f"duration={args.duration:g}s DTR=False RTS=False log={log_path}",
                file=sys.stderr,
            )

            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                try:
                    port.timeout = min(SERIAL_READ_TIMEOUT_SECONDS, remaining)
                    waiting = int(getattr(port, "in_waiting", 0))
                    data = port.read(waiting or 1)
                except (KeyboardInterrupt,):
                    raise
                if not data:
                    continue
                log_file.write(data)
                log_file.flush()
                captured_bytes += len(data)
                _write_stdout(data)
        except KeyboardInterrupt:
            interrupted = True
            print("\n收到 Ctrl+C，停止采集并生成摘要。", file=sys.stderr)
        except Exception as error:  # 串口异常也要保留已经采集到的日志
            serial_error = f"{type(error).__name__}: {error}"
            print(f"串口采集失败：{serial_error}", file=sys.stderr)
        finally:
            if port is not None:
                _set_line_state(port)
                try:
                    if getattr(port, "is_open", False):
                        port.close()
                except (OSError, ValueError) as error:
                    if serial_error is None:
                        serial_error = f"关闭串口失败：{error}"

    ended_at = datetime.now().astimezone()
    return {
        "started_at": started_at.isoformat(),
        "ended_at": ended_at.isoformat(),
        "elapsed_seconds": round(time.monotonic() - started_monotonic, 3),
        "duration_seconds_requested": args.duration,
        "captured_bytes": captured_bytes,
        "interrupted": interrupted,
        "serial_error": serial_error,
        "serial_opened": opened,
        "serial_session_count": 1 if opened else 0,
        "dtr": False,
        "rts": False,
    }


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    log_path = args.log if args.log is not None else _default_log_path()
    capture = capture_serial(args, log_path)
    try:
        raw_log = log_path.read_bytes()
    except OSError as error:
        raw_log = b""
        capture["serial_error"] = capture["serial_error"] or f"读取日志失败：{error}"

    parsed = parse_log(raw_log)
    summary: dict[str, Any] = {
        "schema_version": 1,
        "tool": "capture_voice_stability",
        "port": args.port,
        "baud": args.baud,
        "log": str(log_path.resolve()),
        **capture,
        **parsed,
    }

    # 串口原始输出使用 buffer，切回文本 JSON 前先同步两个层次的缓冲区。
    try:
        sys.stdout.flush()
        if raw_log and not raw_log.endswith((b"\n", b"\r")):
            sys.stdout.write("\n")
        print(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=False), flush=True)
    except (BrokenPipeError, OSError):
        pass

    if capture["interrupted"]:
        return 130
    if capture["serial_error"]:
        return 2
    if parsed["failure"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
