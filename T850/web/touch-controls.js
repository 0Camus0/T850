(() => {
  const panel = document.getElementById('touch-controls');
  const toggle = document.getElementById('touch-enabled');
  const toggleLabel = document.getElementById('touch-toggle');
  const pointers = new Map();
  const axes = [0, 0, 0, 0];
  let memory;
  let enabled = false;
  let ready = false;
  const coarse = matchMedia('(any-pointer: coarse)');
  const supported = () => navigator.maxTouchPoints > 0 || coarse.matches;
  function publish(pressed = 0) {
    if (!memory) return;
    let buttons = 0;
    for (const pointer of pointers.values()) buttons |= pointer.button ?? 0;
    for (let axis = 0; axis < axes.length; ++axis) Atomics.store(memory, axis + 1, Math.round(axes[axis] * 1000));
    Atomics.store(memory, 5, buttons);
    if (pressed) Atomics.or(memory, 6, pressed);
    Atomics.store(memory, 0, enabled && ready && !document.hidden ? 1 : 0);
  }
  function reset() {
    const captured = [...pointers.entries()];
    pointers.clear();
    axes.fill(0);
    for (const [pointerId, pointer] of captured) {
      if (pointer.element.hasPointerCapture(pointerId)) pointer.element.releasePointerCapture(pointerId);
      pointer.element.classList.remove('held');
    }
    for (const stick of panel.querySelectorAll('.touch-stick')) {
      stick.style.setProperty('--stick-x', '0px');
      stick.style.setProperty('--stick-y', '0px');
    }
    if (memory) Atomics.store(memory, 6, 0);
    publish();
  }
  function refresh() {
    panel.hidden = !enabled || !ready;
    toggle.checked = enabled;
    document.body.classList.toggle('touch-mode', enabled && ready);
    if (!enabled || !ready) reset();
    publish();
  }
  function moveStick(event, pointer) {
    const rect = pointer.element.getBoundingClientRect();
    const radius = rect.width * 0.35;
    let horizontal = (event.clientX - rect.left - rect.width / 2) / radius;
    let vertical = (event.clientY - rect.top - rect.height / 2) / radius;
    const length = Math.hypot(horizontal, vertical);
    if (length > 1) { horizontal /= length; vertical /= length; }
    if (length < 0.12) horizontal = vertical = 0;
    axes[pointer.axis] = horizontal;
    axes[pointer.axis + 1] = vertical;
    pointer.element.style.setProperty('--stick-x', `${horizontal * radius}px`);
    pointer.element.style.setProperty('--stick-y', `${vertical * radius}px`);
    publish();
  }
  for (const element of panel.querySelectorAll('[data-axis], [data-button]')) {
    element.addEventListener('pointerdown', event => {
      if (!enabled || !ready || event.button > 0) return;
      event.preventDefault();
      event.stopPropagation();
      if ([...pointers.values()].some(pointer => pointer.element === element)) return;
      const pointer = element.hasAttribute('data-axis')
        ? { element, axis: Number(element.dataset.axis) }
        : { element, button: Number(element.dataset.button) };
      pointers.set(event.pointerId, pointer);
      element.setPointerCapture(event.pointerId);
      element.classList.add('held');
      if (pointer.axis !== undefined) moveStick(event, pointer);
      else publish(pointer.button);
    });
    element.addEventListener('pointermove', event => {
      const pointer = pointers.get(event.pointerId);
      if (pointer?.axis === undefined) return;
      event.preventDefault();
      moveStick(event, pointer);
    });
    const release = event => {
      const pointer = pointers.get(event.pointerId);
      if (!pointer) return;
      pointers.delete(event.pointerId);
      element.classList.remove('held');
      if (pointer.axis !== undefined) {
        axes[pointer.axis] = axes[pointer.axis + 1] = 0;
        element.style.setProperty('--stick-x', '0px');
        element.style.setProperty('--stick-y', '0px');
      }
      publish();
    };
    for (const name of ['pointerup', 'pointercancel', 'lostpointercapture']) element.addEventListener(name, release);
    element.addEventListener('contextmenu', event => event.preventDefault());
  }
  toggle.addEventListener('change', () => { enabled = toggle.checked; refresh(); });
  const detect = () => {
    toggleLabel.hidden = false;
    enabled = supported();
    refresh();
  };
  const onTouch = event => {
    if (event.pointerType === 'touch' && toggleLabel.hidden && window.t850?.scene === 'Minecraft') {
      enabled = true;
      toggleLabel.hidden = false;
      refresh();
    }
  };
  window.addEventListener('pointerdown', onTouch, { capture: true, passive: true });
  coarse.addEventListener('change', () => { if (window.t850?.scene === 'Minecraft') detect(); });
  window.addEventListener('t850-runtime-ready', () => {
    if (window.t850?.scene !== 'Minecraft') return;
    ready = true;
    if (supported()) detect();
    refresh();
  });
  for (const name of ['blur', 'pagehide', 'resize']) window.addEventListener(name, reset);
  document.addEventListener('visibilitychange', reset);
  window.addEventListener('error', () => { ready = false; refresh(); });
  window.addEventListener('unhandledrejection', () => { ready = false; refresh(); });
  window.t850Touch = {
    attach(buffer) { memory = buffer; reset(); refresh(); },
    detach() { reset(); if (memory) Atomics.store(memory, 0, 0); memory = undefined; ready = false; refresh(); },
    reset,
  };
})();