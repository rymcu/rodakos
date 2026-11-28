<script setup lang="ts">
const props = defineProps<{
  code: string
  title?: string
}>()

const copyKey = useId()
const { copiedKey, copyText } = useClipboardCopy()
const copied = computed(() => copiedKey.value === copyKey)
const activeTab = shallowRef<'diagram' | 'table' | 'source'>('diagram')

interface Actor {
  id: string
  label: string
}

type SeqItem =
  | {
      kind: 'message'
      step: number
      from: string
      to: string
      dashed: boolean
      text: string
      depth: number
    }
  | {
      kind: 'note'
      actors: string[]
      text: string
      depth: number
    }
  | {
      kind: 'block-start'
      blockType: 'loop' | 'alt' | 'else'
      label: string
      depth: number
    }
  | {
      kind: 'block-end'
      depth: number
    }

interface StateTransition {
  step: number
  from: string
  to: string
  label: string
}

interface ParsedDiagram {
  kind: 'sequence' | 'state' | 'unknown'
  actors: Actor[]
  seqItems: SeqItem[]
  states: string[]
  transitions: StateTransition[]
}

function charUnits(ch: string): number {
  const cp = ch.codePointAt(0) ?? 0
  if (cp > 0x2e80) return 1.85
  if (ch === 'i' || ch === 'l' || ch === '1' || ch === '.' || ch === ':' || ch === ',') return 0.65
  if (ch === 'W' || ch === 'M' || ch === 'm' || ch === 'w') return 1.25
  return 1.0
}

function wrapText(raw: string, maxUnits: number): string[] {
  const text = raw.trim()
  if (!text) return []
  const lines: string[] = []
  let current = ''
  let units = 0

  for (const ch of text) {
    const w = charUnits(ch)
    if (units + w > maxUnits && current.length > 0) {
      // Prefer breaking cleanly at a nearby ASCII space if one exists near the tail
      const lastSpace = current.lastIndexOf(' ')
      if (lastSpace > current.length * 0.55 && ch !== ' ') {
        lines.push(current.slice(0, lastSpace).trimEnd())
        const remainder = current.slice(lastSpace + 1) + ch
        current = remainder
        units = [...remainder].reduce((sum, c) => sum + charUnits(c), 0)
      } else {
        lines.push(current.trimEnd())
        current = ch === ' ' ? '' : ch
        units = ch === ' ' ? 0 : w
      }
    } else {
      current += ch
      units += w
    }
  }
  if (current.trim()) lines.push(current.trimEnd())
  return lines
}

const parsed = computed<ParsedDiagram>(() => {
  const lines = props.code
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter(Boolean)

  const header = lines[0] ?? ''
  if (header.startsWith('sequenceDiagram')) {
    const actors: Actor[] = []
    const actorMap = new Map<string, Actor>()
    const ensureActor = (id: string, label?: string) => {
      const existing = actorMap.get(id)
      if (existing) return existing
      const created: Actor = { id, label: label ?? id }
      actors.push(created)
      actorMap.set(id, created)
      return created
    }

    const seqItems: SeqItem[] = []
    let step = 0
    let depth = 0

    for (const line of lines.slice(1)) {
      if (line === 'autonumber') continue
      const pMatch = /^participant\s+(\w+)\s+as\s+(.+)$/.exec(line)
      if (pMatch) {
        ensureActor(pMatch[1]!, pMatch[2]!.trim())
        continue
      }
      if (line === 'end') {
        depth = Math.max(0, depth - 1)
        seqItems.push({ kind: 'block-end', depth })
        continue
      }
      const blkMatch = /^(loop|alt|else)\s+(.+)$/.exec(line)
      if (blkMatch) {
        const blockType = blkMatch[1] as 'loop' | 'alt' | 'else'
        if (blockType === 'else') {
          seqItems.push({
            kind: 'block-start',
            blockType: 'else',
            label: blkMatch[2]!.trim(),
            depth: Math.max(0, depth - 1)
          })
        } else {
          seqItems.push({
            kind: 'block-start',
            blockType,
            label: blkMatch[2]!.trim(),
            depth
          })
          depth += 1
        }
        continue
      }
      const noteMatch = /^Note\s+over\s+([^:]+):\s*(.+)$/.exec(line)
      if (noteMatch) {
        const refIds = noteMatch[1]!
          .split(',')
          .map((s) => s.trim())
          .filter(Boolean)
        refIds.forEach((id) => ensureActor(id))
        seqItems.push({
          kind: 'note',
          actors: refIds,
          text: noteMatch[2]!.trim(),
          depth
        })
        continue
      }
      const msgMatch = /^(\w+)\s*(-->>|->>|-->)\s*(\w+)\s*:\s*(.*)$/.exec(line)
      if (msgMatch) {
        const from = msgMatch[1]!
        const arrow = msgMatch[2]!
        const to = msgMatch[3]!
        ensureActor(from)
        ensureActor(to)
        step += 1
        seqItems.push({
          kind: 'message',
          step,
          from,
          to,
          dashed: arrow.startsWith('--'),
          text: msgMatch[4]!.trim(),
          depth
        })
      }
    }

    return { kind: 'sequence', actors, seqItems, states: [], transitions: [] }
  }

  if (header.startsWith('stateDiagram')) {
    const statesSet = new Set<string>()
    const transitions: StateTransition[] = []
    let step = 0
    for (const line of lines.slice(1)) {
      const m = /^(\[\*\]|\w+)\s*-->\s*(\[\*\]|\w+)(?:\s*:\s*(.*))?$/.exec(line)
      if (!m) continue
      const from = m[1]!
      const to = m[2]!
      if (from !== '[*]') statesSet.add(from)
      if (to !== '[*]') statesSet.add(to)
      step += 1
      transitions.push({
        step,
        from,
        to,
        label: (m[3] ?? '').trim()
      })
    }
    return {
      kind: 'state',
      actors: [],
      seqItems: [],
      states: Array.from(statesSet),
      transitions
    }
  }

  return { kind: 'unknown', actors: [], seqItems: [], states: [], transitions: [] }
})

const diagramKindLabel = computed(() => {
  if (props.title) return props.title
  if (parsed.value.kind === 'sequence') return '交互时序图 · Sequence Diagram'
  if (parsed.value.kind === 'state') return '状态机流转图 · State Diagram'
  return 'Mermaid 图示'
})

const stepCount = computed(() => {
  if (parsed.value.kind === 'sequence') {
    return parsed.value.seqItems.filter((i) => i.kind === 'message').length
  }
  return parsed.value.transitions.length
})

const actorLabelById = computed(() => {
  const map = new Map<string, string>()
  for (const a of parsed.value.actors) map.set(a.id, a.label)
  return map
})

// Layout calculation for crisp SVG sequence diagram
const seqLayout = computed(() => {
  const actors = parsed.value.actors
  const count = Math.max(actors.length, 2)
  const colSpacing = count <= 2 ? 320 : count === 3 ? 250 : 210
  const padX = Math.max(96, Math.round(colSpacing * 0.5))
  const width = padX * 2 + (count - 1) * colSpacing

  const colX = new Map<string, number>()
  const actorNodes = actors.map((actor, idx) => {
    const x = padX + idx * colSpacing
    colX.set(actor.id, x)
    const labelLines = wrapText(actor.label, count <= 2 ? 26 : 19)
    const boxW = Math.min(colSpacing - 20, count <= 2 ? 230 : 186)
    const boxH = Math.max(40, 18 + labelLines.length * 16)
    return { ...actor, x, boxW, boxH, labelLines }
  })

  const headerBottomY = 20 + Math.max(...actorNodes.map((a) => a.boxH), 44)
  let y = headerBottomY + 24

  interface PositionedMessage {
    kind: 'message'
    step: number
    x1: number
    x2: number
    y: number
    isSelf: boolean
    dashed: boolean
    lines: string[]
    boxX: number
    boxY: number
    boxW: number
    boxH: number
  }
  interface PositionedNote {
    kind: 'note'
    x: number
    y: number
    w: number
    h: number
    lines: string[]
  }
  interface PositionedBlock {
    blockType: 'loop' | 'alt' | 'else'
    label: string
    x: number
    y: number
    w: number
    h: number
    dividerY?: number
    elseLabel?: string
  }

  const messages: PositionedMessage[] = []
  const notes: PositionedNote[] = []
  const blocks: PositionedBlock[] = []
  const openBlocks: { idx: number; depth: number }[] = []

  for (const item of parsed.value.seqItems) {
    if (item.kind === 'block-start') {
      if (item.blockType === 'else' && openBlocks.length > 0) {
        const top = openBlocks[openBlocks.length - 1]!
        const blk = blocks[top.idx]
        if (blk) {
          blk.dividerY = y
          blk.elseLabel = item.label
        }
        y += 30
      } else {
        const inset = 16 + item.depth * 10
        blocks.push({
          blockType: item.blockType,
          label: item.label,
          x: inset,
          y: y - 4,
          w: width - inset * 2,
          h: 40
        })
        openBlocks.push({ idx: blocks.length - 1, depth: item.depth })
        y += 34
      }
      continue
    }

    if (item.kind === 'block-end') {
      const closed = openBlocks.pop()
      if (closed) {
        const blk = blocks[closed.idx]
        if (blk) {
          blk.h = y - blk.y + 8
        }
      }
      y += 18
      continue
    }

    if (item.kind === 'note') {
      const xs = item.actors.map((id) => colX.get(id) ?? padX)
      const minX = Math.min(...xs)
      const maxX = Math.max(...xs)
      const spanW = Math.max(maxX - minX + 168, Math.min(width - 48, 360))
      const clampedW = Math.min(spanW, width - 48)
      const centerX = (minX + maxX) / 2
      const boxX = Math.max(24, Math.min(width - 24 - clampedW, centerX - clampedW / 2))
      const maxUnits = Math.max(24, Math.floor((clampedW - 28) / 7.1))
      const lines = wrapText(item.text, maxUnits)
      const boxH = 16 + lines.length * 16
      notes.push({ kind: 'note', x: boxX, y, w: clampedW, h: boxH, lines })
      y += boxH + 16
      continue
    }

    if (item.kind === 'message') {
      const x1 = colX.get(item.from) ?? padX
      const x2 = colX.get(item.to) ?? padX
      const isSelf = item.from === item.to

      if (isSelf) {
        const labelW = Math.min(240, width - x1 - 44)
        const maxUnits = Math.max(18, Math.floor(labelW / 7.2))
        const lines = wrapText(item.text, maxUnits)
        const boxH = Math.max(24, lines.length * 16 + 8)
        messages.push({
          kind: 'message',
          step: item.step,
          x1,
          x2,
          y: y + 12,
          isSelf: true,
          dashed: item.dashed,
          lines,
          boxX: x1 + 42,
          boxY: y + 4,
          boxW: Math.max(140, labelW),
          boxH
        })
        y += Math.max(46, boxH + 22)
      } else {
        const span = Math.abs(x2 - x1)
        const labelMaxW = Math.max(180, Math.min(span - 24, width - 72))
        const maxUnits = Math.max(22, Math.floor((labelMaxW - 26) / 7.0))
        const lines = wrapText(item.text, maxUnits)
        const boxH = 10 + lines.length * 16
        const midX = (x1 + x2) / 2
        const boxX = Math.max(24, Math.min(width - 24 - labelMaxW, midX - labelMaxW / 2))
        const arrowY = y + boxH + 10
        messages.push({
          kind: 'message',
          step: item.step,
          x1,
          x2,
          y: arrowY,
          isSelf: false,
          dashed: item.dashed,
          lines,
          boxX,
          boxY: y,
          boxW: labelMaxW,
          boxH
        })
        y = arrowY + 22
      }
    }
  }

  const footerTopY = y + 6
  const height = footerTopY + Math.max(...actorNodes.map((a) => a.boxH), 44) + 18

  return { width, height, headerBottomY, footerTopY, actorNodes, messages, notes, blocks }
})

// Structured state layout for stateDiagram-v2
const stateLayout = computed(() => {
  const states = parsed.value.states
  const transitions = parsed.value.transitions
  const width = 760
  const nodeW = 132
  const nodeH = 40

  // Deterministic two-row layout for clean readability without overlapping labels
  const coords = new Map<string, { x: number; y: number }>()
  if (states.length === 5 && states.includes('unbound') && states.includes('bound')) {
    coords.set('unbound', { x: 118, y: 120 })
    coords.set('pending', { x: 315, y: 62 })
    coords.set('awaiting', { x: 525, y: 62 })
    coords.set('bound', { x: 645, y: 185 })
    coords.set('unbinding', { x: 365, y: 218 })
  } else {
    const cols = Math.min(4, Math.max(2, states.length))
    states.forEach((s, idx) => {
      const col = idx % cols
      const row = Math.floor(idx / cols)
      coords.set(s, {
        x: 110 + col * 180,
        y: 78 + row * 120
      })
    })
  }

  const startPos = { x: 26, y: (coords.get(states[0] ?? '')?.y ?? 120) }
  const nodes = states.map((id) => ({
    id,
    x: coords.get(id)?.x ?? 120,
    y: coords.get(id)?.y ?? 100
  }))

  const edges = transitions.map((tr) => {
    const fromPt = tr.from === '[*]' ? startPos : (coords.get(tr.from) ?? { x: 120, y: 100 })
    const toPt = tr.to === '[*]' ? startPos : (coords.get(tr.to) ?? { x: 260, y: 100 })
    const dx = toPt.x - fromPt.x
    const dy = toPt.y - fromPt.y
    const dist = Math.hypot(dx, dy) || 1
    const startOffset = tr.from === '[*]' ? 10 : Math.min(nodeW * 0.46, Math.abs(dx) > Math.abs(dy) ? nodeW / 2 : nodeH / 2 + 8)
    const endOffset = tr.to === '[*]' ? 12 : Math.min(nodeW * 0.48, Math.abs(dx) > Math.abs(dy) ? nodeW / 2 + 4 : nodeH / 2 + 10)
    const x1 = fromPt.x + (dx / dist) * startOffset
    const y1 = fromPt.y + (dy / dist) * startOffset
    const x2 = toPt.x - (dx / dist) * endOffset
    const y2 = toPt.y - (dy / dist) * endOffset
    // Slight arc curve so bidirectional/adjacent edges stay distinct
    const nx = -dy / dist
    const ny = dx / dist
    const curve = tr.from === 'pending' && tr.to === 'unbound' ? -36 : tr.from === 'awaiting' && tr.to === 'unbound' ? -56 : 16
    const cx = (x1 + x2) / 2 + nx * curve
    const cy = (y1 + y2) / 2 + ny * curve
    return {
      ...tr,
      path: `M ${x1.toFixed(1)} ${y1.toFixed(1)} Q ${cx.toFixed(1)} ${cy.toFixed(1)} ${x2.toFixed(1)} ${y2.toFixed(1)}`,
      badgeX: (x1 + 2 * cx + x2) / 4,
      badgeY: (y1 + 2 * cy + y2) / 4
    }
  })

  const maxY = Math.max(...nodes.map((n) => n.y), 180) + 64
  return { width, height: maxY, nodeW, nodeH, startPos, nodes, edges }
})

const tableRows = computed(() => {
  if (parsed.value.kind === 'sequence') {
    return parsed.value.seqItems.map((item, index) => {
      if (item.kind === 'message') {
        const fromLabel = actorLabelById.value.get(item.from) ?? item.from
        const toLabel = actorLabelById.value.get(item.to) ?? item.to
        return {
          key: `m-${index}`,
          step: String(item.step).padStart(2, '0'),
          direction: `${fromLabel} → ${toLabel}`,
          text: item.text,
          isGroup: false
        }
      }
      if (item.kind === 'note') {
        const who = item.actors.map((a) => actorLabelById.value.get(a) ?? a).join(' / ')
        return {
          key: `n-${index}`,
          step: '注',
          direction: who,
          text: item.text,
          isGroup: false
        }
      }
      if (item.kind === 'block-start') {
        const map = { loop: '循环', alt: '分支', else: '否则' }
        return {
          key: `b-${index}`,
          step: map[item.blockType],
          direction: item.label,
          text: '',
          isGroup: true
        }
      }
      return {
        key: `e-${index}`,
        step: '结束',
        direction: '结束分支 / 循环',
        text: '',
        isGroup: true
      }
    })
  }

  return parsed.value.transitions.map((t) => ({
    key: `t-${t.step}`,
    step: String(t.step).padStart(2, '0'),
    direction: `${t.from === '[*]' ? '起点' : t.from} → ${t.to === '[*]' ? '终点' : t.to}`,
    text: t.label || '初始状态',
    isGroup: false
  }))
})

const markerId = useId()
</script>

<template>
  <figure class="my-6 overflow-hidden rounded-xl border border-default bg-elevated/40 shadow-xs">
    <figcaption
      class="flex flex-wrap items-center justify-between gap-2 border-b border-default bg-elevated/70 px-3.5 py-2"
    >
      <div class="flex items-center gap-2 text-xs font-medium text-highlighted">
        <UIcon
          :name="parsed.kind === 'state' ? 'i-lucide-git-branch' : 'i-lucide-workflow'"
          class="size-4 text-primary"
        />
        <span>{{ diagramKindLabel }}</span>
        <UBadge
          v-if="stepCount > 0"
          :label="`${stepCount} 步`"
          color="primary"
          variant="subtle"
          size="xs"
          class="font-mono"
        />
      </div>

      <div class="flex items-center gap-1.5">
        <div class="inline-flex rounded-md border border-default bg-default/80 p-0.5 text-[11px]">
          <button
            type="button"
            class="cursor-pointer rounded px-2 py-0.5 transition-colors"
            :class="
              activeTab === 'diagram'
                ? 'bg-primary/15 font-medium text-primary'
                : 'text-muted hover:text-highlighted'
            "
            @click="activeTab = 'diagram'"
          >
            图示
          </button>
          <button
            type="button"
            class="cursor-pointer rounded px-2 py-0.5 transition-colors"
            :class="
              activeTab === 'table'
                ? 'bg-primary/15 font-medium text-primary'
                : 'text-muted hover:text-highlighted'
            "
            @click="activeTab = 'table'"
          >
            步骤表
          </button>
          <button
            type="button"
            class="cursor-pointer rounded px-2 py-0.5 transition-colors"
            :class="
              activeTab === 'source'
                ? 'bg-primary/15 font-medium text-primary'
                : 'text-muted hover:text-highlighted'
            "
            @click="activeTab = 'source'"
          >
            Mermaid 源码
          </button>
        </div>

        <UButton
          :icon="copied ? 'i-lucide-check' : 'i-lucide-copy'"
          color="neutral"
          variant="ghost"
          size="xs"
          aria-label="复制 Mermaid 源码"
          @click="copyText(copyKey, code)"
        />
      </div>
    </figcaption>

    <!-- 视图 1：自适应暗/亮色 SVG 图示 -->
    <div v-show="activeTab === 'diagram'" class="overflow-x-auto bg-default/55 p-3 sm:p-4">
      <!-- Sequence Diagram SVG -->
      <svg
        v-if="parsed.kind === 'sequence'"
        :viewBox="`0 0 ${seqLayout.width} ${seqLayout.height}`"
        :style="{ minWidth: `${Math.min(seqLayout.width, 640)}px` }"
        class="mx-auto block h-auto w-full max-w-full select-none"
        role="img"
        :aria-label="diagramKindLabel"
      >
        <defs>
          <marker
            :id="`${markerId}-solid`"
            viewBox="0 0 10 10"
            refX="9"
            refY="5"
            markerWidth="7"
            markerHeight="7"
            orient="auto-start-reverse"
          >
            <path d="M 0 1.5 L 10 5 L 0 8.5 z" fill="var(--ui-primary)" />
          </marker>
          <marker
            :id="`${markerId}-dashed`"
            viewBox="0 0 10 10"
            refX="9"
            refY="5"
            markerWidth="7"
            markerHeight="7"
            orient="auto-start-reverse"
          >
            <path d="M 0 1.5 L 10 5 L 0 8.5 z" fill="var(--rd-signal-cyan)" />
          </marker>
        </defs>

        <!-- Loop / Alt grouping frames -->
        <g v-for="(blk, bi) in seqLayout.blocks" :key="`blk-${bi}`">
          <rect
            :x="blk.x"
            :y="blk.y"
            :width="blk.w"
            :height="blk.h"
            rx="10"
            fill="color-mix(in srgb, var(--ui-primary) 5%, transparent)"
            stroke="color-mix(in srgb, var(--ui-primary) 32%, var(--ui-border))"
            stroke-width="1.2"
            stroke-dasharray="5 4"
          />
          <rect
            :x="blk.x"
            :y="blk.y"
            width="52"
            height="22"
            rx="6"
            fill="color-mix(in srgb, var(--ui-primary) 18%, var(--ui-bg-elevated))"
          />
          <text
            :x="blk.x + 26"
            :y="blk.y + 15"
            text-anchor="middle"
            fill="var(--ui-primary)"
            font-size="10.5"
            font-weight="700"
            font-family="var(--font-mono)"
          >
            {{ blk.blockType.toUpperCase() }}
          </text>
          <text
            :x="blk.x + 62"
            :y="blk.y + 15"
            fill="var(--ui-text-highlighted)"
            font-size="11.5"
            font-weight="600"
          >
            {{ blk.label }}
          </text>
          <g v-if="blk.dividerY">
            <line
              :x1="blk.x"
              :y1="blk.dividerY"
              :x2="blk.x + blk.w"
              :y2="blk.dividerY"
              stroke="color-mix(in srgb, var(--ui-primary) 35%, var(--ui-border))"
              stroke-dasharray="4 4"
            />
            <rect
              :x="blk.x"
              :y="blk.dividerY"
              width="52"
              height="20"
              rx="4"
              fill="color-mix(in srgb, var(--ui-primary) 14%, var(--ui-bg-elevated))"
            />
            <text
              :x="blk.x + 26"
              :y="blk.dividerY + 13.5"
              text-anchor="middle"
              fill="var(--ui-primary)"
              font-size="10"
              font-weight="700"
              font-family="var(--font-mono)"
            >
              ELSE
            </text>
            <text
              v-if="blk.elseLabel"
              :x="blk.x + 62"
              :y="blk.dividerY + 14"
              fill="var(--ui-text-muted)"
              font-size="11.5"
              font-weight="500"
            >
              {{ blk.elseLabel }}
            </text>
          </g>
        </g>

        <!-- Lifelines -->
        <line
          v-for="actor in seqLayout.actorNodes"
          :key="`line-${actor.id}`"
          :x1="actor.x"
          :y1="seqLayout.headerBottomY"
          :x2="actor.x"
          :y2="seqLayout.footerTopY"
          stroke="var(--ui-border-accented)"
          stroke-width="1.4"
          stroke-dasharray="4 4"
        />

        <!-- Top & Bottom Participant Nodes -->
        <g v-for="actor in seqLayout.actorNodes" :key="`head-${actor.id}`">
          <rect
            :x="actor.x - actor.boxW / 2"
            y="20"
            :width="actor.boxW"
            :height="actor.boxH"
            rx="10"
            fill="var(--ui-bg-elevated)"
            stroke="color-mix(in srgb, var(--ui-primary) 42%, var(--ui-border))"
            stroke-width="1.3"
          />
          <text
            :x="actor.x"
            :y="20 + (actor.boxH - actor.labelLines.length * 16) / 2 + 12"
            text-anchor="middle"
            fill="var(--ui-text-highlighted)"
            font-size="12"
            font-weight="600"
          >
            <tspan
              v-for="(ln, li) in actor.labelLines"
              :key="li"
              :x="actor.x"
              :dy="li === 0 ? 0 : 16"
            >
              {{ ln }}
            </tspan>
          </text>

          <rect
            :x="actor.x - actor.boxW / 2"
            :y="seqLayout.footerTopY"
            :width="actor.boxW"
            :height="actor.boxH"
            rx="10"
            fill="var(--ui-bg-elevated)"
            stroke="var(--ui-border)"
            stroke-width="1.1"
            opacity="0.88"
          />
          <text
            :x="actor.x"
            :y="seqLayout.footerTopY + (actor.boxH - actor.labelLines.length * 16) / 2 + 12"
            text-anchor="middle"
            fill="var(--ui-text-muted)"
            font-size="11.5"
            font-weight="500"
          >
            <tspan
              v-for="(ln, li) in actor.labelLines"
              :key="li"
              :x="actor.x"
              :dy="li === 0 ? 0 : 16"
            >
              {{ ln }}
            </tspan>
          </text>
        </g>

        <!-- Note Over Callouts -->
        <g v-for="(note, ni) in seqLayout.notes" :key="`note-${ni}`">
          <rect
            :x="note.x"
            :y="note.y"
            :width="note.w"
            :height="note.h"
            rx="8"
            fill="color-mix(in srgb, var(--ui-primary) 10%, var(--ui-bg-elevated))"
            stroke="color-mix(in srgb, var(--ui-primary) 38%, var(--ui-border))"
            stroke-width="1.1"
          />
          <text
            :x="note.x + note.w / 2"
            :y="note.y + 19"
            text-anchor="middle"
            fill="var(--ui-text-toned)"
            font-size="11.5"
          >
            <tspan
              v-for="(ln, li) in note.lines"
              :key="li"
              :x="note.x + note.w / 2"
              :dy="li === 0 ? 0 : 16"
            >
              {{ ln }}
            </tspan>
          </text>
        </g>

        <!-- Messages & Arrows -->
        <g v-for="msg in seqLayout.messages" :key="`msg-${msg.step}`">
          <template v-if="msg.isSelf">
            <path
              :d="`M ${msg.x1} ${msg.y} H ${msg.x1 + 32} V ${msg.y + 22} H ${msg.x1 + 3}`"
              fill="none"
              :stroke="msg.dashed ? 'var(--rd-signal-cyan)' : 'var(--ui-primary)'"
              stroke-width="1.6"
              :stroke-dasharray="msg.dashed ? '5 3' : undefined"
              :marker-end="`url(#${markerId}-${msg.dashed ? 'dashed' : 'solid'})`"
            />
            <circle :cx="msg.x1" :cy="msg.y" r="9.5" fill="var(--ui-primary)" />
            <text
              :x="msg.x1"
              :y="msg.y + 3.5"
              text-anchor="middle"
              fill="#ffffff"
              font-size="9.5"
              font-weight="700"
              font-family="var(--font-mono)"
            >
              {{ msg.step }}
            </text>
            <rect
              :x="msg.boxX"
              :y="msg.boxY"
              :width="msg.boxW"
              :height="msg.boxH"
              rx="6"
              fill="var(--ui-bg)"
              stroke="var(--ui-border)"
              stroke-width="1"
              opacity="0.96"
            />
            <text
              :x="msg.boxX + 10"
              :y="msg.boxY + 15"
              fill="var(--ui-text-highlighted)"
              font-size="11.5"
            >
              <tspan
                v-for="(ln, li) in msg.lines"
                :key="li"
                :x="msg.boxX + 10"
                :dy="li === 0 ? 0 : 16"
              >
                {{ ln }}
              </tspan>
            </text>
          </template>

          <template v-else>
            <rect
              :x="msg.boxX"
              :y="msg.boxY"
              :width="msg.boxW"
              :height="msg.boxH"
              rx="6"
              fill="var(--ui-bg)"
              fill-opacity="0.92"
              stroke="var(--ui-border)"
              stroke-width="0.9"
            />
            <text
              :x="msg.boxX + msg.boxW / 2"
              :y="msg.boxY + 15"
              text-anchor="middle"
              fill="var(--ui-text-highlighted)"
              font-size="11.5"
            >
              <tspan
                v-for="(ln, li) in msg.lines"
                :key="li"
                :x="msg.boxX + msg.boxW / 2"
                :dy="li === 0 ? 0 : 16"
              >
                {{ ln }}
              </tspan>
            </text>
            <line
              :x1="msg.x1"
              :y1="msg.y"
              :x2="msg.x2 + (msg.x2 > msg.x1 ? -3 : 3)"
              :y2="msg.y"
              :stroke="msg.dashed ? 'var(--rd-signal-cyan)' : 'var(--ui-primary)'"
              stroke-width="1.6"
              :stroke-dasharray="msg.dashed ? '6 4' : undefined"
              :marker-end="`url(#${markerId}-${msg.dashed ? 'dashed' : 'solid'})`"
            />
            <circle :cx="msg.x1" :cy="msg.y" r="9.5" :fill="msg.dashed ? 'var(--rd-signal-cyan)' : 'var(--ui-primary)'" />
            <text
              :x="msg.x1"
              :y="msg.y + 3.5"
              text-anchor="middle"
              fill="#07111b"
              font-size="9.5"
              font-weight="700"
              font-family="var(--font-mono)"
            >
              {{ msg.step }}
            </text>
          </template>
        </g>
      </svg>

      <!-- State Diagram SVG + Indexed Transition Legend -->
      <div v-else-if="parsed.kind === 'state'" class="space-y-3">
        <svg
          :viewBox="`0 0 ${stateLayout.width} ${stateLayout.height}`"
          class="mx-auto block h-auto w-full max-w-full min-w-[540px] select-none"
          role="img"
          :aria-label="diagramKindLabel"
        >
          <defs>
            <marker
              :id="`${markerId}-state`"
              viewBox="0 0 10 10"
              refX="8.5"
              refY="5"
              markerWidth="7"
              markerHeight="7"
              orient="auto-start-reverse"
            >
              <path d="M 0 1.5 L 10 5 L 0 8.5 z" fill="var(--ui-primary)" />
            </marker>
          </defs>

          <!-- Start indicator -->
          <circle
            :cx="stateLayout.startPos.x"
            :cy="stateLayout.startPos.y"
            r="8"
            fill="var(--ui-primary)"
          />

          <!-- Curved transition edges with numbered badges -->
          <g v-for="edge in stateLayout.edges" :key="`edge-${edge.step}`">
            <path
              :d="edge.path"
              fill="none"
              stroke="color-mix(in srgb, var(--ui-primary) 72%, var(--ui-border))"
              stroke-width="1.6"
              :marker-end="`url(#${markerId}-state)`"
            />
            <circle :cx="edge.badgeX" :cy="edge.badgeY" r="10" fill="var(--ui-bg-elevated)" stroke="var(--ui-primary)" stroke-width="1.2" />
            <text
              :x="edge.badgeX"
              :y="edge.badgeY + 3.5"
              text-anchor="middle"
              fill="var(--ui-primary)"
              font-size="9.5"
              font-weight="700"
              font-family="var(--font-mono)"
            >
              {{ edge.step }}
            </text>
          </g>

          <!-- State Nodes -->
          <g v-for="node in stateLayout.nodes" :key="node.id">
            <rect
              :x="node.x - stateLayout.nodeW / 2"
              :y="node.y - stateLayout.nodeH / 2"
              :width="stateLayout.nodeW"
              :height="stateLayout.nodeH"
              rx="10"
              fill="var(--ui-bg-elevated)"
              stroke="color-mix(in srgb, var(--ui-primary) 50%, var(--ui-border))"
              stroke-width="1.5"
            />
            <text
              :x="node.x"
              :y="node.y + 4"
              text-anchor="middle"
              fill="var(--ui-text-highlighted)"
              font-size="12.5"
              font-weight="600"
              font-family="var(--font-mono)"
            >
              {{ node.id }}
            </text>
          </g>
        </svg>

        <div class="grid gap-1.5 border-t border-default pt-3 sm:grid-cols-2">
          <div
            v-for="tr in parsed.transitions"
            :key="`legend-${tr.step}`"
            class="flex items-start gap-2 rounded-lg border border-default bg-elevated/45 px-2.5 py-1.5 text-xs"
          >
            <span
              class="mt-0.5 inline-flex size-4 shrink-0 items-center justify-center rounded-full bg-primary/15 font-mono text-[10px] font-bold text-primary"
            >
              {{ tr.step }}
            </span>
            <div class="min-w-0">
              <span class="font-mono font-semibold text-highlighted">
                {{ tr.from === '[*]' ? '起点' : tr.from }} → {{ tr.to }}
              </span>
              <span v-if="tr.label" class="text-muted">：{{ tr.label }}</span>
            </div>
          </div>
        </div>
      </div>

      <pre v-else class="overflow-x-auto text-xs text-muted">{{ code }}</pre>
    </div>

    <!-- 视图 2：结构化步骤表 -->
    <div v-show="activeTab === 'table'" class="overflow-x-auto bg-default/55">
      <table class="my-0! w-full border-collapse text-left text-xs">
        <thead>
          <tr class="border-b border-default bg-elevated/60 text-muted">
            <th class="w-16 px-3 py-2 font-mono font-semibold">序号</th>
            <th class="w-56 px-3 py-2 font-semibold">方向 / 阶段</th>
            <th class="px-3 py-2 font-semibold">交互内容</th>
          </tr>
        </thead>
        <tbody>
          <tr
            v-for="row in tableRows"
            :key="row.key"
            class="border-b border-default/60 last:border-b-0"
            :class="row.isGroup ? 'bg-primary/8 font-medium text-primary' : ''"
          >
            <td class="px-3 py-2 font-mono text-primary">{{ row.step }}</td>
            <td class="px-3 py-2 font-medium text-highlighted">{{ row.direction }}</td>
            <td class="px-3 py-2 text-muted">{{ row.text }}</td>
          </tr>
        </tbody>
      </table>
    </div>

    <!-- 视图 3：原始 Mermaid 源码 -->
    <div v-show="activeTab === 'source'" class="overflow-x-auto bg-default/80 p-3.5">
      <pre class="m-0 font-mono text-xs leading-relaxed text-toned"><code>{{ code }}</code></pre>
    </div>
  </figure>
</template>
