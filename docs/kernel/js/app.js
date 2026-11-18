(function () {
  const navLinks = Array.from(document.querySelectorAll('.nav a[data-section]'));
  const sections = Array.from(document.querySelectorAll('.section'));

  function showSection(id, pushHash) {
    const target = document.getElementById(id) ? id : 'inicio';
    sections.forEach(s => s.classList.toggle('active', s.id === target));
    navLinks.forEach(a => a.classList.toggle('active', a.dataset.section === target));
    if (pushHash) {
      history.replaceState(null, '', '#' + target);
    }
    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  navLinks.forEach(a => {
    a.addEventListener('click', (e) => {
      e.preventDefault();
      showSection(a.dataset.section, true);
    });
  });

  // Tabs within cards
  document.querySelectorAll('[data-tabs]').forEach(group => {
    const buttons = Array.from(group.querySelectorAll('.tab-btn'));
    const panels = Array.from(group.querySelectorAll('.tab-panel'));
    buttons.forEach(btn => {
      btn.addEventListener('click', () => {
        const name = btn.dataset.tab;
        buttons.forEach(b => b.classList.toggle('active', b === btn));
        panels.forEach(p => p.classList.toggle('active', p.dataset.tab === name));
      });
    });
  });

  // Simple in-page jump links
  document.querySelectorAll('a[data-jump]').forEach(a => {
    a.addEventListener('click', (e) => {
      e.preventDefault();
      showSection(a.dataset.jump, true);
    });
  });

  // Gallery loader
  async function loadGallery() {
    const grid = document.getElementById('gallery-grid');
    const status = document.getElementById('gallery-status');
    if (!grid) return;
    try {
      const res = await fetch('gallery.json', { cache: 'no-store' });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const items = await res.json();
      if (!Array.isArray(items) || items.length === 0) {
        status.textContent = 'No hay imágenes en gallery.json. Añade entradas para mostrarlas.';
        return;
      }
      grid.innerHTML = '';
      items.forEach(item => {
        const card = document.createElement('div');
        card.className = 'gallery-item';
        const img = document.createElement('img');
        img.loading = 'lazy';
        img.alt = item.title || 'Imagen de galería';
        img.src = item.src;
        img.onerror = () => { img.alt = 'No se pudo cargar: ' + item.src; };
        const meta = document.createElement('div');
        meta.className = 'meta';
        const title = document.createElement('div');
        title.className = 'title';
        title.textContent = item.title || 'Sin título';
        const desc = document.createElement('div');
        desc.className = 'desc';
        desc.textContent = item.desc || '';
        meta.appendChild(title);
        meta.appendChild(desc);
        card.appendChild(img);
        card.appendChild(meta);
        grid.appendChild(card);
      });
      status.textContent = 'Mostrando ' + items.length + ' elemento(s) desde gallery.json.';
    } catch (err) {
      status.textContent = 'No se pudo cargar gallery.json (' + err.message + '). Si abres el archivo localmente, usa un servidor (ej. `python3 -m http.server`) para permitir fetch.';
    }
  }

  // Initial route
  const hash = (location.hash || '#inicio').replace('#', '');
  showSection(hash, false);
  window.addEventListener('hashchange', () => {
    const h = (location.hash || '#inicio').replace('#', '');
    showSection(h, false);
  });

  loadGallery();
})();