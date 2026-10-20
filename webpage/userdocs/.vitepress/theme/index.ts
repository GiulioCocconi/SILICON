import DefaultTheme from "vitepress/theme";
import LessonLayout from "./layouts/LessonLayout.vue";
import ParentLink from "./components/ParentLink.vue";
import "./style.css";

export default {
  extends: DefaultTheme,
  Layout: LessonLayout,
  enhanceApp({ app }) {
    app.component("ParentLink", ParentLink);
  },
};
