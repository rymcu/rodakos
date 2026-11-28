export type SiteLocale = 'zh' | 'en'

export type FirmwareThemeId = 'dark' | 'light' | 'blue' | 'green'

export type ScreenSceneId =
  | 'home'
  | 'home-page2'
  | 'voice-caption'
  | 'calendar'
  | 'gyro'
  | 'smart'
  | 'themes'
  | 'themes-trust'
  | 'assistant'
  | 'recorder'
  | 'wol'
  | 'clock'
  | 'settings'
  | 'system'
  | 'music'
  | 'files'
  | 'photos'
  | 'camera'
  | 'messages'

export type HeroPreviewTabId =
  | 'home'
  | 'voice-caption'
  | 'calendar'
  | 'gyro'
  | 'smart'
  | 'themes'

export type AppCategoryId = 'system' | 'voice-smart' | 'media' | 'tools'

export type VoiceStageId = 'wake' | 'connect' | 'duplex' | 'caption' | 'announce'

export type BuildTabId = 'firmware' | 'host-tests'

export interface SiteNavLink {
  label: string
  to: string
  hash?: string
}

export interface HighlightCardItem {
  id: string
  icon: string
  title: string
  description: string
  badge: string
}

export interface ScreenGalleryItem {
  id: ScreenSceneId
  label: string
  icon: string
  title: string
  alt: string
  caption: string
  darkSrc: string
  lightSrc?: string
  supportsFirmwareThemes?: boolean
}

export interface HeroTabMeta extends ScreenGalleryItem {
  id: HeroPreviewTabId
}

export interface ShowcaseUiCopy {
  realRenderBadge: string
  demoDataNote: string
  themeLabel: string
  themeNames: Record<FirmwareThemeId, string>
  expandLabel: string
  zoomInLabel: string
  fitLabel: string
  closeLabel: string
  mobileHint: string
  prevSlideLabel: string
  nextSlideLabel: string
  carouselHint: string
}

export interface ShellFeatureItem {
  icon: string
  title: string
  description: string
}

export interface BuiltinAppEntry {
  id: string
  name: string
  icon: string
  slotColor: string
  summary: string
  offline: boolean
}

export interface AppCategoryGroup {
  id: AppCategoryId
  label: string
  icon: string
  sceneId: ScreenSceneId
  screenTitle: string
  screenCaption: string
  screenAlt: string
  darkSrc: string
  lightSrc?: string
  note: string
  apps: readonly BuiltinAppEntry[]
}

export interface VoicePipelineStage {
  id: VoiceStageId
  step: string
  title: string
  engine: string
  metric: string
  icon: string
  summary: string
  highlights: readonly string[]
}

export interface EcosystemProjectCard {
  id: 'hardware' | 'rodak'
  badge: string
  repoLabel: string
  title: string
  description: string
  icon: string
  to: string
  specs: readonly string[]
  ctaLabel: string
}

export interface PartitionSegment {
  name: string
  subType: string
  offset: string
  sizeLabel: string
  widthPercent: number
  colorClass: string
  role: string
}

export interface BuildPathCard {
  badge: string
  title: string
  description: string
  icon: string
  highlights: readonly string[]
  primaryLabel: string
  primaryTo: string
  primaryExternal?: boolean
  secondaryLabel: string
  secondaryTo: string
  secondaryExternal?: boolean
}

export interface ReleaseGateItem {
  issue: string
  url: string
  label: string
}

export interface FaqAccordionItem {
  label: string
  icon: string
  content: string
}

export interface FooterLinkGroup {
  label: string
  children: readonly {
    label: string
    to: string
    target?: '_blank'
    external?: boolean
  }[]
}

export interface LandingCopy {
  meta: {
    title: string
    description: string
  }
  nav: {
    links: readonly SiteNavLink[]
    docsLabel: string
    protocolsLabel: string
    githubCta: string
    searchLabel: string
    localeToggleLabel: string
  }
  hero: {
    badge: string
    titleLine1: string
    titleLine2: string
    description: string
    primaryCta: string
    secondaryCta: string
    docsCta: string
    hint: string
    previewTabs: readonly HeroTabMeta[]
    showcase: ShowcaseUiCopy
  }
  highlights: {
    headline: string
    title: string
    description: string
    items: readonly HighlightCardItem[]
  }
  appsShell: {
    headline: string
    title: string
    description: string
    offlineBadge: string
    cloudBadge: string
    features: readonly ShellFeatureItem[]
    categories: readonly AppCategoryGroup[]
  }
  voice: {
    headline: string
    title: string
    description: string
    diagramBadge: string
    diagramNote: string
    labCta: string
    handbookCta: string
    stages: readonly VoicePipelineStage[]
    cards: readonly {
      icon: string
      title: string
      description: string
    }[]
  }
  hardware: {
    headline: string
    title: string
    description: string
    projects: readonly EcosystemProjectCard[]
    enclosureHeadline: string
    enclosureTitle: string
    enclosureDescription: string
    enclosureCta: string
    enclosureFeatures: readonly ShellFeatureItem[]
    partitionTitle: string
    partitionBadge: string
    partitionSummary: string
    partitions: readonly PartitionSegment[]
    enclosureImageCaption: string
    enclosureImageAlt: string
  }
  buildFlash: {
    headline: string
    title: string
    description: string
    quickstart: BuildPathCard
    developer: BuildPathCard
    commandTabs: {
      firmwareLabel: string
      hostTestsLabel: string
      firmwareTitle: string
      hostTestsTitle: string
      firmwareCode: string
      hostTestsCode: string
      firmwareNote: string
      hostTestsNote: string
    }
    copyLabel: string
    copiedLabel: string
    readinessTitle: string
    readinessBadge: string
    readinessDesc: string
    readinessGates: readonly ReleaseGateItem[]
    readinessCta: string
  }
  faq: {
    headline: string
    title: string
    description: string
    items: readonly FaqAccordionItem[]
    ctaTitle: string
    ctaDescription: string
    primaryCta: string
    secondaryCta: string
    tertiaryCta: string
  }
  footer: {
    tagline: string
    copyright: string
    columns: readonly FooterLinkGroup[]
  }
}
