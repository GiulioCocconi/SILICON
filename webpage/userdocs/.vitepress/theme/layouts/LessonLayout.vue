<script setup lang="ts">
import { watchEffect } from "vue";
import DefaultTheme from "vitepress/theme";
import { useData } from "vitepress";
import { data as lessons } from "../lessons.data";

const { theme } = useData();

watchEffect(() => {
  theme.value.sidebar["/lessons/"] = [{ text: "Lessons", items: lessons }];
});
</script>

<template>
  <DefaultTheme.Layout />
</template>
