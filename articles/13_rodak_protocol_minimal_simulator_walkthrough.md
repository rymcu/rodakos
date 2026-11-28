# 零硬件上手：Python 协议联调片段与离线 RAV1 验证

> **参考规范**：以下为独立的联调片段与离线解析示例，使用仓库 AIoT 与语音正例作字段参考。绑定需要在隔离测试服务手动确认；语音片段没有发送真实上行 Opus，不能证明 ASR、TTS 或对话闭环。

> 示例中的 1.4.0 是[绑定与 Ping 夹具值](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/contracts/rodak-aiot/v1/examples/valid/binding-request.rodakos.json)，不是当前固件版本。若要验证接受、进度与字幕事件，请参考 [session-open.caption 正例](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/contracts/rodak-realtime-voice/v1/examples/valid/session-open.caption.json) 与 [output-caption 正例](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/contracts/rodak-realtime-voice/v1/examples/valid/output-caption.first-sentence.json)，在独立用例里提供有效上行语音与协商所需的 VAD 行为；不要将只握手的收包片段当作事件验收。[RAV1 正例](https://github.com/rymcu/rodakos/blob/38f16b0d2f916106349fd93206edf16854ccae8f/contracts/rodak-realtime-voice/v1/examples/valid/rav1-frame.opus.json)只校验封装，不代表载荷适合解码。消息中心快照与动作报文请对照 `contracts/rodak-aiot/v1/examples/valid/inbox-*.json`。

## 1. 环境准备

示例脚本仅依赖 Python 3.10+ 与三个常见库（本地联调时安装）：

```bash
pip install requests 'paho-mqtt>=2,<3' 'websockets>=14,<16'
```

> **联调前确认**：联调用隔离测试服务和专用模拟设备标识。示例只覆盖受信测试网 Legacy 路径；端口按实际部署替换，MQTT 从描述符获取。Pin 模式需另行实现并验证信任锚点、固定 TLS 域名及 HTTPS／MQTTS／WSS，不是仅换 URL。

## 2. 第一步：Bootstrap 强校验与手动绑定（HTTP 控制面）

以下代码完整演示了协议 §2–§4 的两个核心防坑点：① 严格校验 `bootstrap` 固定身份字段；② **先查 HTTP 状态码（处理 401），再查统一信封 `code == 200`**。

```python
import secrets
import time
import uuid
import requests

ORIGIN = "http://rodak.local:9080"  # 示例地址，改为隔离测试服务的实际地址
DEVICE_KEY = "aa:bb:cc:dd:ee:ff"  # 小写 Wi-Fi STA MAC
CLIENT_ID = str(uuid.uuid4())
DEVICE_SECRET = secrets.token_hex(32)  # 32 字节随机数 -> 64 位小写十六进制（严禁打印日志）


def unwrap_envelope(resp: requests.Response) -> dict:
    """严格执行两步检查：先 HTTP 状态码，再信封 code。"""
    if resp.status_code == 401:
        raise PermissionError("HTTP 401: accessToken 预检失败，需刷新凭据")
    resp.raise_for_status()
    body = resp.json()
    if body.get("code") != 200:
        raise RuntimeError(f"业务错误 code={body.get('code')} message={body.get('message')}")
    return body["data"]


def bootstrap_and_bind() -> dict:
    # 1. GET /bootstrap 并执行 §3 强校验
    boot = unwrap_envelope(requests.get(f"{ORIGIN}/api/v1/aiot/devices/bootstrap", timeout=6))
    assert boot.get("module") == "aiot", "拒绝非 aiot 模块"
    assert boot.get("protocol") == "rodak-aiot", "拒绝非 rodak-aiot 协议"
    assert boot.get("protocolVersion") == 1, "拒绝不支持的协议版本"
    assert boot.get("productKey") == "rymcu-bigsmart", "拒绝不匹配的 productKey"

    # 2. POST /binding/request（对齐 binding-request.rodakos.json 夹具）
    req_payload = {
        "protocol": "rodak-aiot",
        "protocolVersion": 1,
        "productKey": "rymcu-bigsmart",
        "product_key": "rymcu-bigsmart",
        "deviceCode": DEVICE_KEY,
        "deviceKey": DEVICE_KEY,
        "deviceSecret": DEVICE_SECRET,
        "credentialSecret": DEVICE_SECRET,
        "deviceName": "RodakOS Python Simulator",
        "clientId": CLIENT_ID,
        "firmwareVersion": "1.4.0",
        "hardwareVersion": "rymcu_bigsmart",  # 注意下划线，区别于 productKey
        "application": {"name": "rodakos", "version": "1.4.0"},
        "board": {"type": "rymcu_bigsmart", "productKey": "rymcu-bigsmart"},
    }
    created = unwrap_envelope(
        requests.post(f"{ORIGIN}/api/v1/aiot/devices/binding/request", json=req_payload, timeout=6)
    )
    request_id = created["requestId"]
    request_token = created["requestToken"]
    poll_interval = int(created.get("pollIntervalSeconds", 2))
    print(f"请在 Rodak 桌面端输入 8 位配对码: {created['pairingCode']}")

    # 3. 轮询绑定状态直至 confirmed
    headers = {"Authorization": f"Bearer {request_token}"}
    deadline = time.monotonic() + 600
    while time.monotonic() < deadline:
        status_data = unwrap_envelope(
            requests.get(
                f"{ORIGIN}/api/v1/aiot/devices/binding/requests/{request_id}/status",
                headers=headers,
                timeout=6,
            )
        )
        st = status_data["status"]
        if st == "confirmed":
            print("绑定成功！已获取 accessToken、unifiedMqtt 与 realtimeVoice")
            return status_data
        if st in ("expired", "revoked"):
            raise RuntimeError(f"绑定终止: {st}")
        time.sleep(min(max(poll_interval, 1), max(0, deadline - time.monotonic())))
    raise TimeoutError("绑定等待超时")
```

## 3. 第二步：连接 Unified MQTT、条件订阅 `inbox/changed` 并响应 `ping`

拿到 `confirmed` 响应后，使用 `unifiedMqtt` 建立连接，订阅 4 个必选下行 Topic（仅当 `topics.inbox_changed` 精确匹配 `devices/{k}/inbox/changed` 时才订阅第 5 个可选 Topic），并演示如何按 `command-ack.pong.json` 规范回复 `ping` 以及处理无正文脏信号。

```python
import json
import paho.mqtt.client as mqtt


def start_mqtt_simulator(confirmed: dict) -> mqtt.Client:
    cfg = confirmed["unifiedMqtt"]
    topics = cfg["topics"]
    device_key = cfg["device_key"]
    expected_inbox = f"devices/{device_key}/inbox/changed"
    inbox_topic = topics.get("inbox_changed") if topics.get("inbox_changed") == expected_inbox else None

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=device_key)
    client.username_pw_set(cfg["username"], cfg["password"])  # password 即 accessToken

    def on_connect(c, userdata, flags, reason_code, properties):
        print(f"MQTT 已连接: {reason_code}")
        for key in ("shadow_desired", "commands", "ota_notify", "pc_status"):
            c.subscribe(topics[key], qos=0)
        if inbox_topic:
            c.subscribe(inbox_topic, qos=0)  # 仅在服务端明确列出时订阅
        # 上线即上报初始遥测与影子
        c.publish(topics["telemetry"], json.dumps({"wifi_rssi": -45}), qos=0)
        c.publish(topics["shadow_report"], json.dumps({"volume": 50}), qos=0)

    def on_message(c, userdata, msg):
        if inbox_topic and msg.topic == inbox_topic:
            # 草案 §7.6：空载荷或 {schemaVersion:1, inboxRevision, unreadCount} 均合法，不含正文
            raw = msg.payload.decode("utf-8").strip()
            signal = json.loads(raw) if raw else {}
            print(f"收到 inbox/changed 脏信号: revision={signal.get('inboxRevision')}，需经 HTTPS 拉取快照")
            return
        # 处理 devices/{k}/commands/{commandNo}
        prefix = f"devices/{device_key}/commands/"
        if msg.topic.startswith(prefix) and not msg.topic.endswith("/ack"):
            command_no = msg.topic[len(prefix):]
            payload = json.loads(msg.payload.decode("utf-8"))
            cmd = payload.get("command")
            if cmd == "ping":
                ack = {"status": "ok", "result": {"pong": True, "firmware": "1.4.0"}}
            else:
                ack = {"status": "error", "errorCode": "unsupported_command"}
            c.publish(f"{prefix}{command_no}/ack", json.dumps(ack), qos=0, retain=False)

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(cfg["broker_address"], int(cfg["broker_port"]), keepalive=int(cfg["keepalive"]))
    client.loop_start()
    return client
```

## 4. 第三步：消息中心快照拉取与幂等动作（HTTPS 草案 §7.6）

收到 `inbox/changed` 或 MQTT 重连上线后，设备使用同一 `accessToken` 经 HTTP 控制面拉取最新快照，并携带设备生成的 `opId` 与 `expectedRevision` 提交已读、完成或推迟动作（网络无应答重试时必须复用同一个 `opId`）：

```python
def fetch_inbox_snapshot(access_token: str) -> dict:
    """GET /api/v1/aiot/devices/inbox，返回 inbox-snapshot。"""
    headers = {"Authorization": f"Bearer {access_token}"}
    return unwrap_envelope(requests.get(f"{ORIGIN}/api/v1/aiot/devices/inbox", headers=headers, timeout=8))


def apply_inbox_action(
    access_token: str,
    *,
    op_id: str,
    action: str,
    notice_id: str,
    expected_revision: int,
    task_id: str | None = None,
    occurrence_id: str | None = None,
    snooze_minutes: int | None = None,
) -> dict:
    """POST /api/v1/aiot/devices/inbox/actions，遵守 read/complete/snooze 字段互斥。"""
    body = {
        "opId": op_id,
        "action": action,
        "noticeId": notice_id,
        "expectedRevision": expected_revision,
    }
    if action == "complete":
        body["taskId"] = task_id
    elif action == "snooze":
        assert snooze_minutes in (10, 30, 60)
        body["occurrenceId"] = occurrence_id
        body["snoozeMinutes"] = snooze_minutes
    headers = {"Authorization": f"Bearer {access_token}"}
    return unwrap_envelope(
        requests.post(f"{ORIGIN}/api/v1/aiot/devices/inbox/actions", headers=headers, json=body, timeout=8)
    )
```

## 5. 第四步：实时语音握手、字幕协商与 RAV1 二进制帧编解码

以下异步代码演示了如何按 `rodak-realtime-voice/v1` 规范携带鉴权头建连、根据描述符 `features` 条件协商 `output_caption: 1`、发送 `session.open` / `wake.detected` / `input.start`，以及严格校验下行 **RAV1** 二进制帧与 `output.caption` 事件（可直接用仓库夹具 `52415631000000010000000778a1b2c3d4e5f6` 单元测试 `parse_rav1_frame`）。

```python
import asyncio
import json
import struct
import websockets


def parse_rav1_frame(frame: bytes, last_seq: int, max_audio_bytes: int = 8192) -> tuple[int, bytes]:
    """解析并强校验 RAV1 (12 + N) 二进制音频帧。"""
    if len(frame) < 12 or frame[:4] != b"RAV1":
        raise ValueError("RAV1 魔数或头部长度无效")
    seq, payload_len = struct.unpack(">II", frame[4:12])
    if seq == 0 or seq <= last_seq:
        raise ValueError(f"RAV1 序列号非法或未严格递增: seq={seq}, last={last_seq}")
    if payload_len > max_audio_bytes or len(frame) - 12 != payload_len:
        raise ValueError(f"RAV1 载荷长度不匹配或超限: declared={payload_len}, actual={len(frame) - 12}")
    return seq, frame[12:]


async def run_voice_session(confirmed: dict, generation: int = 1) -> None:
    rv = confirmed["realtimeVoice"]
    endpoint = rv["endpoint"]
    max_audio = int(rv["limits"]["maxAudioFrameBytes"])
    wants_caption = "output_caption" in (rv.get("features") or [])
    features = {
        "mcp": False,
        "device_vad_epoch": 1,
        "turn_accepted": 1,
        "turn_progress": 1,
    }
    if wants_caption:
        # 契约 §6.2：仅在描述符 features 列出 output_caption 时请求
        features["output_caption"] = 1

    headers = {
        "Authorization": f"Bearer {confirmed['accessToken']}",
        "Protocol-Version": "1",
        "Device-Id": DEVICE_KEY,
        "Client-Id": CLIENT_ID,
    }

    async with websockets.connect(endpoint, additional_headers=headers, open_timeout=10) as ws:
        # 1. 发送 session.open（对齐 session-open.caption.json / session-open.turn-progress.json）
        await ws.send(json.dumps({
            "event": "session.open",
            "protocol": "rodak-realtime-voice",
            "protocolVersion": 1,
            "generation": generation,
            "vadStrategies": ["device-authoritative", "server-authoritative", "hybrid-fallback"],
            "preferredVadStrategy": "hybrid-fallback",
            "features": features,
            "uplink": {"codec": "opus", "sampleRateHz": 16000, "channels": 1, "frameDurationMs": 60},
            "downlink": {"codec": "opus", "sampleRateHz": 24000, "channels": 1, "frameDurationMs": 60},
        }))

        # 2. 等待匹配 generation 的 session.ready
        ready = json.loads(await asyncio.wait_for(ws.recv(), timeout=10))
        assert ready.get("event") == "session.ready"
        assert ready.get("generation") == generation
        session_id = ready["sessionId"].strip()
        assert session_id, "sessionId 不能为空"

        # 3. 首轮唤醒发送 wake.detected + input.start(realtime)
        await ws.send(json.dumps({"event": "wake.detected", "sessionId": session_id, "text": "你好达克"}))
        await ws.send(json.dumps({"event": "input.start", "sessionId": session_id, "mode": "realtime"}))

        # 4. 仅握手／收包演示；无上行语音时不等待实际回复
        last_seq = 0
        deadline = asyncio.get_running_loop().time() + 10
        while (remaining := deadline - asyncio.get_running_loop().time()) > 0:
            try:
                msg = await asyncio.wait_for(ws.recv(), timeout=remaining)
            except asyncio.TimeoutError:
                break  # 没送上行语音，收到空闲超时属于正常退出
            if isinstance(msg, bytes):
                last_seq, opus_packet = parse_rav1_frame(msg, last_seq, max_audio)
                print(f"收到 RAV1 音频帧 seq={last_seq}, opus_bytes={len(opus_packet)}")
            else:
                evt = json.loads(msg)
                name = evt.get("event")
                if name == "output.caption":
                    # 设备端不将字幕正文写入日志，仅记录轮次、序号、帧区间与字节长度
                    print(
                        f"收到 output.caption: epoch={evt['playbackEpoch']} seq={evt['captionSeq']} "
                        f"frames={evt['firstAudioSeq']}..{evt['lastAudioSeq']} "
                        f"bytes={len(evt['text'].encode('utf-8'))}"
                    )
                else:
                    print(f"收到控制事件: {name}")
                if name == "session.end":
                    break
```

## 6. 本地校验你的模拟器是否符合契约

在开始针对真实 Rodak 服务端联调之前，建议先用仓库自带的契约与夹具验证你的解析逻辑：

1. **验证 RAV1 夹具**：将 `contracts/rodak-realtime-voice/v1/examples/valid/rav1-frame.opus.json` 中的十六进制 `"52415631000000010000000778a1b2c3d4e5f6"` 转为字节送入 `parse_rav1_frame`，应成功解出 `seq = 1` 与 7 字节载荷；将 `examples/invalid/rav1-frame.*.json` 送入则必须抛出异常。
2. **核对字幕与消息中心夹具**：参照 `contracts/rodak-realtime-voice/v1/examples/{valid,invalid}/output-caption.*.json`、`session-open.*caption*.json` 以及 `contracts/rodak-aiot/v1/examples/{valid,invalid}/inbox-*.json` 与 `unified-mqtt.inbox-changed.json` 校验自身请求与解析分支。
3. **全量契约一致性检查**：运行 `python3 tools/check_aiot_contract.py`，确保你新增或修改的报文结构与 `contracts/rodak-aiot/v1/`（32 Schema / 154 例）及 `contracts/rodak-realtime-voice/v1/`（16 Schema / 70 例）完全对齐。
