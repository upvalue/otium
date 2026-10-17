// ✨ Astro renders repository Markdown; assets and page links stay prefix-relative.
import { defineConfig } from 'astro/config';
import { dirname, resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { navigation, repository } from './navigation.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const published = new Set(navigation.map(([slug]) => resolve(root, 'docs', `${slug}.md`)));

function documentationLinks() {
  return (tree, file) => {
    const source = String(file.path).includes('/.generated/')
      ? resolve(root, 'docs/reference.md') : String(file.path);
    function visit(node) {
      if ((node.type === 'link' || node.type === 'definition') && node.url && !/^(?:[a-z]+:|\/|#)/i.test(node.url)) {
        const match = node.url.match(/^([^?#]*)(.*)$/);
        const target = resolve(dirname(source), decodeURIComponent(match[1]));
        const suffix = match[2];
        node.url = published.has(target)
          ? `./${relative(resolve(root, 'docs'), target).replace(/\.md$/, '.html')}${suffix}`
          : `${repository}${relative(root, target)}${suffix}`;
      }
      node.children?.forEach(visit);
    }
    visit(tree);
  };
}

// ✨ Symbol-only binding names need explicit unique heading IDs.
function referenceHeadings() {
  return (tree, file) => {
    if (!String(file.path).endsWith('/.generated/reference.md')) return;
    let index = 0;
    function visit(node) {
      if (node.type === 'element' && node.tagName === 'h2') {
        node.properties ??= {};
        node.properties.id = `binding-title-${index++}`;
      }
      node.children?.forEach(visit);
    }
    visit(tree);
  };
}

export default defineConfig({
  output: 'static',
  build: { format: 'file' },
  markdown: {
    remarkPlugins: [documentationLinks],
    rehypePlugins: [referenceHeadings],
    shikiConfig: { theme: 'github-dark' },
  },
});
