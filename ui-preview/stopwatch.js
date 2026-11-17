const lapRecords = [];
function refreshStopwatchActions() {
  document.querySelectorAll('[data-reset]').forEach(button => {
    button.textContent = !running && elapsed > 0 ? '复位' : '计圈';
    button.disabled = !running && elapsed === 0;
  });
  document.querySelectorAll('[data-records]').forEach(button => {
    button.textContent = `记录 ${lapRecords.length}`;
    button.disabled = lapRecords.length === 0;
  });
}
document.addEventListener('click', event => {
  const button = event.target.closest('button');
  if (!button) return;
  if (button.hasAttribute('data-reset')) {
    event.stopImmediatePropagation();
    if (running) lapRecords.push(elapsed + performance.now() - startTime);
    else if (elapsed > 0) { elapsed = 0; lapRecords.length = 0; }
    refreshStopwatchActions();
  }
  if (button.hasAttribute('data-records')) {
    event.stopImmediatePropagation();
    const rows = lapRecords.map((time, index) => `<div class="record-row"><span>第${index + 1}圈</span><span>${new Date(time).toISOString().slice(11,22)}</span></div>`).reverse().join('');
    button.closest('.screen').insertAdjacentHTML('beforeend', `<div class="records-page"><div class="title">计圈记录</div><button class="back" data-close-records aria-label="返回秒表">${icon('back')}</button><div class="records-list">${rows}</div></div>`);
  }
  if (button.hasAttribute('data-close-records')) {
    event.stopImmediatePropagation();
    button.closest('.records-page').remove();
  }
}, true);
document.addEventListener('click', () => queueMicrotask(refreshStopwatchActions));
refreshStopwatchActions();
