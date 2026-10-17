// ✨ Filter the generated binding reference without a search service.
const search = document.querySelector('#reference-search');
if (search) {
  const entries = [...document.querySelectorAll('.reference-entry')];
  const update = () => {
    const query = search.value.trim().toLowerCase();
    let count = 0;
    for (const entry of entries) {
      entry.hidden = !entry.textContent.toLowerCase().includes(query);
      if (!entry.hidden) count++;
    }
    document.querySelector('#reference-count').textContent = `${count} of ${entries.length} bindings`;
  };
  search.addEventListener('input', update);
  update();
}
