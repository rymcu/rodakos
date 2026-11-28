<script setup lang="ts">
const { copy } = useLandingCopy()

useSeoMeta({
  title: () => copy.value.meta.title,
  description: () => copy.value.meta.description,
  ogTitle: () => copy.value.meta.title,
  ogDescription: () => copy.value.meta.description
})
</script>

<template>
  <div>
    <LandingHeroSection />
    <LandingHighlightsSection />
    <LandingAppsShellSection />
    <LandingVoiceProtocolSection />
    <LandingHardwareEcosystemSection />
    <LandingBuildFlashSection />
    <LandingFaqCtaSection />
  </div>
</template>
