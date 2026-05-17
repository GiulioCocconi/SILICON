<script setup lang="ts">
import { withBase } from 'vitepress'
defineProps<{ to: string }>();
</script>

<template>
	<a :href="`${withBase('/..')}${to}`" target="_self">
    <slot />
  </a>
</template>
