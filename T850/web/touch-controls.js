(() => {
  const panel = document.getElementById('touch-controls');
  const toggle = document.getElementById('touch-enabled');
  const toggleLabel = document.getElementById('touch-toggle');
  const onTop = document.getElementById('touch-ontop');
  const onTopLabel = document.getElementById('touch-ontop-toggle');
  const cameraControls = document.getElementById('camera-controls');
  const commands = [...document.querySelectorAll('[data-command]')];
  const pointers = new Map();
  const axes = [0, 0, 0, 0];
  let memory;
  let enabled = false;
  let ready = false;
  let layout = '';
  let userChoice = null;
  let observedTouch = false;
  const coarse = matchMedia('(any-pointer: coarse)');
  const supported = () => observedTouch || navigator.maxTouchPoints > 0 || coarse.matches;
  const mobileDevice = () => {
    const agent = navigator.userAgent ?? '';
    const platform = navigator.userAgentData?.platform ?? navigator.platform ?? '';
    if (/Windows|Win32|Win64|CrOS|Chrome OS/i.test(platform + ' ' + agent)) return false;
    if (/Android|iPhone|iPad|iPod/i.test(platform + ' ' + agent)) return true;
    if (/MacIntel|Macintosh/.test((navigator.platform ?? '') + ' ' + agent) && navigator.maxTouchPoints > 1) return true;
    return navigator.userAgentData?.mobile === true;
  };
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
    for (const stick of panel.querySelectorAll('.touch-stick')) {
      stick.style.setProperty('--stick-x', '0px');
      stick.style.setProperty('--stick-y', '0px');
    }
    if (memory) Atomics.store(memory, 6, 0);
    publish();
    for (const [pointerId, pointer] of captured) {
      pointer.element.classList.remove('held');
      try {
        if (pointer.element.hasPointerCapture(pointerId)) pointer.element.releasePointerCapture(pointerId);
      } catch (error) {
        if (error.name !== 'InvalidStateError' && error.name !== 'NotFoundError') throw error;
      }
    }
  }
  function refresh() {
    panel.hidden = !enabled || !ready;
    toggle.checked = enabled;
    document.body.classList.toggle('touch-mode', enabled && ready);
    onTopLabel.hidden = toggleLabel.hidden;
    onTop.disabled = !enabled || !ready;
    document.body.classList.toggle('touch-ontop', enabled && ready && onTop.checked);
    cameraControls.hidden = !ready || enabled;
    for (const command of commands) command.disabled = !ready || !memory;
    if (!enabled || !ready) reset();
    const nextLayout = enabled && ready ? (onTop.checked ? 'ontop' : 'touch') : '';
    if (layout !== nextLayout) {
      layout = nextLayout;
      window.dispatchEvent(new Event('resize'));
    }
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
      try {
        element.setPointerCapture(event.pointerId);
        if (!element.hasPointerCapture(event.pointerId)) return;
      } catch (error) {
        if (error.name === 'InvalidStateError' || error.name === 'NotFoundError') return;
        throw error;
      }
      pointers.set(event.pointerId, pointer);
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
  toggle.addEventListener('change', () => { userChoice = toggle.checked; enabled = userChoice; refresh(); });
  for (const command of commands) {
    command.addEventListener('click', event => {
      event.preventDefault();
      event.stopPropagation();
      if (!ready || !memory) return;
      reset();
      Atomics.xor(memory, 7, Number(command.dataset.command));
    });
  }
  onTop.addEventListener('change', () => { reset(); refresh(); });
  const detect = () => {
    toggleLabel.hidden = !supported() && userChoice === null;
    enabled = userChoice ?? (supported() && mobileDevice());
    refresh();
  };
  const onTouch = event => {
    if (event.pointerType === 'touch' && !observedTouch && ready && window.t850?.scene === 'Minecraft') {
      observedTouch = true;
      detect();
    }
  };
  window.addEventListener('pointerdown', onTouch, { capture: true, passive: true });
  coarse.addEventListener('change', () => { if (window.t850?.scene === 'Minecraft') detect(); });
  window.addEventListener('t850-runtime-ready', () => {
    if (window.t850?.scene !== 'Minecraft') return;
    ready = true;
    detect();
  });
  for (const name of ['blur', 'pagehide', 'resize']) window.addEventListener(name, reset);
  document.addEventListener('visibilitychange', reset);
  window.addEventListener('error', () => { ready = false; refresh(); });
  window.addEventListener('unhandledrejection', () => { ready = false; refresh(); });
  window.t850Touch = {
    attach(buffer) { memory = buffer; reset(); refresh(); },
    detach() { reset(); if (memory) { Atomics.store(memory, 0, 0); Atomics.store(memory, 7, 0); } memory = undefined; ready = false; refresh(); },
    updateCamera(camera) {
      for (const command of commands) {
        const view = command.dataset.command === '1';
        const pressed = view ? camera.mode === 1 : camera.invertY;
        command.setAttribute('aria-pressed', String(pressed));
        command.title = view ? (pressed ? 'Return to first person' : 'Switch to free spectator') : (pressed ? 'Restore normal vertical look' : 'Invert vertical look');
      }
    },
    reset,
  };
})();