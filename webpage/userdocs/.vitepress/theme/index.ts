import DefaultTheme from "vitepress/theme";
import ParentLink from "./components/ParentLink.vue";
import "./style.css";

export default {
  extends: DefaultTheme,
  enhanceApp({ app }) {
    app.component("ParentLink", ParentLink);
  },
};
