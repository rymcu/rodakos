<script setup lang="ts">
import { buildSignatureString } from '../../../shared/protocol-logic.mjs'

definePageMeta({
  layout: 'protocols'
})

const { t, getFixture } = useProtocolFixtures()
const { withBase } = useSiteAssetUrl()

useSeoMeta({
  title: () => `${t('OTA 签名验证 · 协议实验室', 'OTA Signature Check · Protocol Lab')} · RodakOS`,
  description: () =>
    t(
      '在浏览器本地使用 WebCrypto 验证 RodakOS Manifest v2 的 RSA-2048 PKCS#1 v1.5 SHA-256 签名并查看完整 9 步 OTA 协同流程。',
      'Verify RodakOS Manifest v2 RSA-2048 PKCS#1 v1.5 SHA-256 signatures locally in the browser using WebCrypto alongside the 9-step OTA flow.'
    )
})

const FLOW_ROWS = computed(() => [
  ['MQTT', 'ota/notify {protocolVersion, taskNo, packageCode, fromVersion?, toVersion}', t('订阅时也会投递最新的有效任务。v2 任务没有签名就不会通知。', 'Also delivered on subscribe. Unsigned v2 tasks are never notified.')],
  ['HTTP', 'GET /api/v1/ota/check', t('返回 upgradeAvailable、taskNo、packageCode、版本。', 'Returns upgradeAvailable, taskNo, packageCode and versions.')],
  ['HTTP', 'POST /api/v1/ota/download-ticket {taskNo}', t('返回 ticketId（UUID），600 秒有效。', 'Returns a ticketId (UUID), valid for 600 s.')],
  ['HTTP', 'GET /api/v1/ota/manifest/{taskNo}?ticketId=…', t('只安装 manifestVersion: 2。错误是业务码：403 票据无效，404 无此任务，409 已终结或缺签名。', 'Install only manifestVersion: 2. Business errors: 403 bad ticket, 404 no task, 409 terminal or unsigned.')],
  ['HTTP', 'GET manifest.url', t('200 整包，或 206 对应 Range。非 2xx 的 body 绝不能写入。', '200 full image, or 206 for Range. Never write a non-2xx body.')],
  ['MQTT', 'ota/progress: check → prepare → download → staged', t('QoS 0；RodakOS 对 staged 用 QoS 1。进度只是进度。', 'QoS 0; RodakOS uses QoS 1 for staged. Progress never completes a task.')],
  ['—', t('重启进入 Recovery，写入 ota_0', 'Reboot into Recovery, write ota_0'), t('写入前已验证大小、SHA-256、签名。', 'Size, SHA-256 and signature are verified before writing.')],
  ['HTTP', 'POST /api/v1/ota/tasks/{taskNo}/result {success, detail}', t('唯一能记成功或失败的调用。success 必须是布尔值。', 'The only call recording success or failure. success must be a JSON boolean.')],
  ['MQTT', 'ota/progress: success | failed | result_rejected', t('结果之后的终态进度，只是通知；任务状态以 HTTP result 为准。', 'Terminal progress after result, informational only; HTTP result is authoritative.')]
])

const taskNo = shallowRef('')
const version = shallowRef('')
const fileSize = shallowRef('')
const checksumValue = shallowRef('')
const signatureValue = shallowRef('')
const verifying = shallowRef(false)
const verifyState = shallowRef<{ color: 'success' | 'error'; text: string } | null>(null)

const signingRaw = computed(() =>
  buildSignatureString({
    taskNo: taskNo.value,
    version: version.value,
    fileSize: fileSize.value,
    checksumValue: checksumValue.value
  })
)

const signingDisplay = computed(() => signingRaw.value.replace(/\n/g, '\\n\n'))

watch([taskNo, version, fileSize, checksumValue, signatureValue], () => {
  verifyState.value = null
})

interface SignedManifestFixture {
  taskNo: string
  version: string
  fileSize: number
  checksumValue: string
  signatureValue: string
}

async function loadSignedManifest() {
  const m = await getFixture<SignedManifestFixture>('ota-manifest.signed-v2.json')
  if (!m) return
  taskNo.value = String(m.taskNo ?? '')
  version.value = String(m.version ?? '')
  fileSize.value = String(m.fileSize ?? '')
  checksumValue.value = String(m.checksumValue ?? '')
  signatureValue.value = String(m.signatureValue ?? '')
  verifyState.value = null
}

let cachedKey: CryptoKey | null = null

async function importTestPublicKey(): Promise<CryptoKey> {
  if (cachedKey) return cachedKey
  const res = await fetch(withBase('protocols/data/ota-signing-public-key.pem'))
  if (!res.ok) throw new Error(`public key PEM: HTTP ${res.status}`)
  const pem = await res.text()
  const b64 = pem.replace(/-----[^-]+-----/g, '').replace(/\s+/g, '')
  const der = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0))
  cachedKey = await crypto.subtle.importKey(
    'spki',
    der,
    { name: 'RSASSA-PKCS1-v1_5', hash: 'SHA-256' },
    false,
    ['verify']
  )
  return cachedKey
}

async function runVerify() {
  verifying.value = true
  try {
    if (!crypto.subtle) {
      throw new Error(t('WebCrypto 需要 HTTPS 或 localhost 环境。', 'WebCrypto requires HTTPS or localhost.'))
    }
    const hex = signatureValue.value.trim()
    if (!/^[0-9a-f]{512}$/.test(hex)) {
      verifyState.value = {
        color: 'error',
        text: t(
          'signatureValue 必须是 512 个小写十六进制字符（256 字节 RSA-2048 签名）。',
          'signatureValue must be 512 lowercase hex characters (256-byte RSA-2048 signature).'
        )
      }
      return
    }
    const pairs = hex.match(/../g) ?? []
    const sigBytes = Uint8Array.from(pairs, (b) => Number.parseInt(b, 16))
    const payloadBytes = new TextEncoder().encode(signingRaw.value)
    const key = await importTestPublicKey()
    const ok = await crypto.subtle.verify('RSASSA-PKCS1-v1_5', key, sigBytes, payloadBytes)
    verifyState.value = ok
      ? {
          color: 'success',
          text: t(
            '签名有效：该清单可通过验签，交给后续的文件大小与 SHA-256 完整性校验。',
            'Signature valid: the manifest passes verification and can proceed to size and SHA-256 checks.'
          )
        }
      : {
          color: 'error',
          text: t(
            '签名无效：设备必须拒绝该清单（SIGNATURE_INVALID）。',
            'Signature invalid: the device must reject this manifest (SIGNATURE_INVALID).'
          )
        }
  } catch (err) {
    verifyState.value = { color: 'error', text: String(err) }
  } finally {
    verifying.value = false
  }
}

onMounted(() => {
  void loadSignedManifest()
})
</script>

<template>
  <div class="space-y-8">
    <UPageHeader
      headline="AIoT v1 · §12"
      :title="t('OTA 协同流程与 RSA-2048 浏览器验签', 'OTA Flow & Browser RSA-2048 Signature Check')"
      :description="
        t(
          'MQTT 只负责通知，下载和结果都走 HTTP（带 access token）。设备自己验证大小、SHA-256 和 RSA 签名，再交给不可变 Recovery 写入 ota_0。',
          'MQTT only notifies; downloads and results travel over HTTP with the access token. The device verifies size, SHA-256 and RSA signature itself before immutable Recovery writes ota_0.'
        )
      "
    />

    <!-- 9 步 OTA 协同流程 -->
    <UCard variant="subtle" :ui="{ root: 'ring-1 ring-default', body: 'space-y-4 p-5 sm:p-6' }">
      <h2 class="text-lg font-semibold text-highlighted">
        {{ t('9 步协同流程', '9-step coordinated flow') }}
      </h2>

      <div class="overflow-x-auto rounded-lg border border-default">
        <table class="w-full border-collapse text-left text-xs">
          <thead>
            <tr class="border-b border-default bg-elevated/70 text-muted">
              <th class="w-10 px-3 py-2 font-mono font-semibold">#</th>
              <th class="w-20 px-3 py-2 font-semibold">{{ t('通道', 'Channel') }}</th>
              <th class="px-3 py-2 font-semibold">{{ t('请求 / 动作', 'Request / Action') }}</th>
              <th class="px-3 py-2 font-semibold">{{ t('要点', 'Notes') }}</th>
            </tr>
          </thead>
          <tbody>
            <tr
              v-for="([ch, req, note], idx) in FLOW_ROWS"
              :key="idx"
              class="border-b border-default/60 last:border-b-0"
            >
              <td class="px-3 py-2 font-mono text-primary">{{ idx + 1 }}</td>
              <td class="px-3 py-2">
                <UBadge
                  :label="ch"
                  :color="ch === 'HTTP' ? 'primary' : ch === 'MQTT' ? 'success' : 'neutral'"
                  variant="subtle"
                  size="xs"
                  class="font-mono"
                />
              </td>
              <td class="px-3 py-2 font-mono text-highlighted">{{ req }}</td>
              <td class="px-3 py-2 text-muted">{{ note }}</td>
            </tr>
          </tbody>
        </table>
      </div>

      <UAlert
        icon="i-lucide-alert-circle"
        color="primary"
        variant="subtle"
        :title="t('进度永远不会完成任务', 'Progress never completes a task')"
        :description="
          t(
            '只有 POST /tasks/{taskNo}/result 能记成功或失败，且 success 必须是 JSON 布尔值（字符串 “true” 会返回业务 400 且任务状态不变）。',
            'Only POST /tasks/{taskNo}/result records success or failure, and success must be a JSON boolean (string &quot;true&quot; returns business 400 with the task unchanged).'
          )
        "
      />
    </UCard>

    <!-- 浏览器 WebCrypto 验签器 -->
    <UCard
      id="signature"
      variant="subtle"
      :ui="{ root: 'ring-1 ring-default scroll-mt-28', body: 'space-y-5 p-5 sm:p-6' }"
    >
      <div class="flex flex-wrap items-start justify-between gap-3">
        <div class="space-y-1">
          <h2 class="text-lg font-semibold text-highlighted">
            {{ t('Manifest v2 RSA-2048 签名验证', 'Manifest v2 RSA-2048 signature check') }}
          </h2>
          <p class="text-xs leading-relaxed text-muted">
            {{
              t(
                '签名算法为 RSA PKCS#1 v1.5 SHA-256，覆盖下面固定 8 行原文（每行含 \\n 结尾）。仅覆盖 taskNo、version、fileSize、checksumValue 四个字段，任意改动 1 字节即验签失败。页面调用浏览器原生 WebCrypto 与测试公钥验签。',
                'RSA PKCS#1 v1.5 SHA-256 over the fixed 8-line string below (every line ends with \\n). Changing any byte of taskNo, version, fileSize, or checksumValue fails verification via browser WebCrypto.'
              )
            }}
          </p>
        </div>

        <UButton
          :label="t('重载 ota-manifest.signed-v2.json', 'Reload ota-manifest.signed-v2.json')"
          icon="i-lucide-file-down"
          color="neutral"
          variant="outline"
          size="xs"
          @click="loadSignedManifest"
        />
      </div>

      <div class="grid gap-4 sm:grid-cols-3">
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="ota-task">taskNo</label>
          <UInput id="ota-task" v-model="taskNo" class="w-full font-mono" />
        </div>
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="ota-ver">version</label>
          <UInput id="ota-ver" v-model="version" class="w-full font-mono" />
        </div>
        <div class="space-y-1.5">
          <label class="block text-xs font-medium text-highlighted" for="ota-size">fileSize</label>
          <UInput id="ota-size" v-model="fileSize" class="w-full font-mono" />
        </div>
      </div>

      <div class="space-y-1.5">
        <label class="block text-xs font-medium text-highlighted" for="ota-sha">checksumValue (sha256)</label>
        <UInput id="ota-sha" v-model="checksumValue" class="w-full font-mono" />
      </div>

      <div class="space-y-1.5">
        <label class="block text-xs font-medium text-highlighted" for="ota-sig">signatureValue (512 hex chars)</label>
        <UTextarea id="ota-sig" v-model="signatureValue" :rows="3" class="w-full font-mono" />
      </div>

      <div class="space-y-2 rounded-lg border border-default bg-default/75 p-3.5">
        <div class="flex items-center justify-between">
          <span class="text-xs font-semibold text-highlighted">
            {{ t('待签名 8 行原文', '8-line signing payload') }}
          </span>
          <UBadge label="rsa2048-sha256" color="neutral" variant="subtle" size="xs" class="font-mono" />
        </div>
        <pre class="overflow-x-auto font-mono text-xs leading-relaxed text-toned">{{ signingDisplay }}</pre>
      </div>

      <div class="flex flex-wrap items-center gap-3">
        <UButton
          :label="t('使用 WebCrypto 验证签名', 'Verify signature with WebCrypto')"
          icon="i-lucide-shield-check"
          color="primary"
          size="sm"
          :loading="verifying"
          @click="runVerify"
        />
        <span class="font-mono text-[11px] text-dimmed">
          examples/ota-signing-public-key.pem (test key only)
        </span>
      </div>

      <UAlert
        v-if="verifyState"
        :color="verifyState.color"
        variant="subtle"
        :icon="verifyState.color === 'success' ? 'i-lucide-check-circle-2' : 'i-lucide-x-circle'"
        :description="verifyState.text"
      />

      <p class="text-xs leading-relaxed text-muted">
        {{
          t(
            '真实设备上，签名校验与 SHA-256、文件大小检查全部在主应用侧完成，通过后才重启移交 Recovery。下载路由出错时 HTTP 状态等于业务码（403/404/409）且 body 为 JSON 信封，绝不可写入固件。',
            'On hardware, signature, size, and SHA-256 verification complete before Recovery runs. Non-2xx artifact responses contain JSON error envelopes and must never be flashed.'
          )
        }}
      </p>
    </UCard>
  </div>
</template>
