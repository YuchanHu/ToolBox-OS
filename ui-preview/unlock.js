// Lock-screen drag progress directly drives both layers. No easing while dragging.
let unlockDrag = null;
let ignoreClickUntil = 0;

function paintUnlock(drag, progress) {
  drag.progress = progress;
  drag.face.style.transform = `translateY(${-200 * progress}px) scale(${1 - .15625 * progress})`;
  drag.face.style.opacity = 1 - progress;
  drag.drawer.style.transform = `translateY(${120 * (1 - progress)}px) scale(${.8984375 + .1015625 * progress})`;
  drag.drawer.style.opacity = progress;
}

document.addEventListener('pointerdown', event => {
  const screen = event.target.closest('.screen');
  if (!screen || screen.dataset.settling || Number(screen.dataset.overlay)) return;
  if (!['home', 'analog'].includes(screen.dataset.page)) return;
  unlockDrag = { screen, id: event.pointerId, x: event.clientX, y: event.clientY, progress: 0, velocity: 0, sampled: false, sampleY: event.clientY, sampleTime: performance.now() };
}, true);

document.addEventListener('pointermove', event => {
  const drag = unlockDrag;
  if (!drag || drag.id !== event.pointerId) return;
  const distance = drag.y - event.clientY;
  const time = performance.now(), duration = time - drag.sampleTime;
  if (duration > 0 && event.clientY !== drag.sampleY) {
    const speed = (drag.sampleY - event.clientY) / duration;
    drag.velocity = drag.sampled ? .65 * speed + .35 * drag.velocity : speed;
    drag.sampled = true;
    drag.sampleY = event.clientY;
    drag.sampleTime = time;
  }
  if (!drag.face) {
    if (distance < 6 || distance < Math.abs(event.clientX - drag.x)) return;
    drag.face = document.createElement('div');
    drag.face.className = 'unlock-layer unlock-face';
    drag.face.append(...drag.screen.childNodes);
    drag.drawer = document.createElement('div');
    drag.drawer.className = 'unlock-layer unlock-drawer';
    drag.drawer.innerHTML = content('drawer');
    drag.screen.append(drag.drawer, drag.face);
    drag.screen.setPointerCapture(event.pointerId);
    pointer = null;
  }
  paintUnlock(drag, Math.max(0, Math.min(1, distance / 180)));
}, true);

function finishUnlock(event) {
  const drag = unlockDrag;
  if (!drag || drag.id !== event.pointerId) return;
  unlockDrag = null;
  if (!drag.face) return;
  event.stopImmediatePropagation();
  pointer = null;
  ignoreClickUntil = performance.now() + 350;
  const velocity = performance.now() - drag.sampleTime > 100 ? 0 : drag.velocity;
  const distance = drag.progress * 180;
  const complete = event.type !== 'pointercancel' && velocity > -.15 && (distance >= 64 || (distance >= 16 && velocity >= .30));
  const settleDuration = complete && velocity > 0
    ? Math.max(180, Math.min(280, (1 - drag.progress) * 180 / velocity)) : 280;
  drag.screen.dataset.settling = 'true';
  for (const layer of [drag.face, drag.drawer]) {
    layer.style.transition = `transform ${settleDuration}ms cubic-bezier(.22,1,.36,1), opacity ${settleDuration}ms cubic-bezier(.22,1,.36,1)`;
  }
  paintUnlock(drag, complete ? 1 : 0);
  setTimeout(() => {
    if (complete) {
      drag.screen.replaceChildren(...drag.drawer.childNodes);
      drag.screen.dataset.page = 'drawer';
    } else {
      drag.screen.replaceChildren(...drag.face.childNodes);
    }
    delete drag.screen.dataset.settling;
  }, settleDuration + 10);
}
document.addEventListener('pointerup', finishUnlock, true);
document.addEventListener('pointercancel', finishUnlock, true);
document.addEventListener('click', event => {
  if (performance.now() < ignoreClickUntil) {
    event.preventDefault();
    event.stopImmediatePropagation();
  }
}, true);
