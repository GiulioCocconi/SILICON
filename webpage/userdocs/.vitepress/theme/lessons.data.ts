import { createContentLoader } from "vitepress";

interface LessonLink {
  text: string;
  link: string;
}

declare const data: LessonLink[];
export { data };

export default createContentLoader("lessons/**/*.md", {
  includeSrc: true,
  transform(pages): LessonLink[] {
    return pages
      .map(({ url, frontmatter, src }) => {
        const heading = src?.match(/^#\s+(.+?)\s*#*\s*$/m)?.[1];
        const filename = url.split("/").filter(Boolean).pop() ?? "Lessons";
        return {
          text: String(frontmatter.title ?? heading ?? filename.replace(/\.html$/, "").replace(/[-_]/g, " ")),
          link: url,
          order: typeof frontmatter.order === "number" && Number.isFinite(frontmatter.order)
            ? frontmatter.order
            : url === "/lessons/" ? 0 : Infinity,
        };
      })
      .sort((a, b) => {
        if (a.order !== b.order) return a.order - b.order;
        return a.link.localeCompare(b.link, "en", { numeric: true });
      })
      .map(({ text, link }) => ({ text, link }));
  },
});
