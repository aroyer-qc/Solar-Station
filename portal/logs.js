'use strict';

const SECONDS_PER_DAY = 86400;
const Y_AXIS_HEADROOM = 1.25;
const CURSOR_HEAD_WIDTH = 58;
const CURSOR_HEAD_HEIGHT = 20;
const CURSOR_TIP_HEIGHT = 6;
const CURSOR_GRAB_RADIUS = 12;

const elements = {
    title: document.getElementById('logTitle'),
    subtitle: document.getElementById('logSubtitle'),
    channelSelect: document.getElementById('channelSelect'),
    daySelect: document.getElementById('daySelect'),
    download: document.getElementById('downloadLink'),
    message: document.getElementById('message'),
    canvas: document.getElementById('logChart'),
    readout: document.getElementById('chartReadout'),
    zoomButton: document.getElementById('zoomButton'),
    resetButton: document.getElementById('resetZoomButton'),
    rangeLabel: document.getElementById('rangeLabel'),
    statMin: document.getElementById('statMin'),
    statMax: document.getElementById('statMax'),
    statAvg: document.getElementById('statAvg'),
    statCount: document.getElementById('statCount'),
    statPeriod: document.getElementById('statPeriod')
};

const state = {
    channels: [],
    files: [],
    channel: null,
    file: null,
    samples: null,
    stepSeconds: 1,
    plot: null,
    storageReady: false,
    clockValid: false,
    storageDiag: '',
    view: { start: 0, end: SECONDS_PER_DAY },
    cursors: { start: 0, end: SECONDS_PER_DAY },
    drag: null
};

function setMessage(text) {
    elements.message.textContent = text || '';
}

function formatValue(value, unit) {
    if (!Number.isFinite(value)) return '--';
    const magnitude = Math.abs(value);
    let digits = 3;
    if (magnitude >= 100) digits = 0;
    else if (magnitude >= 10) digits = 1;
    else if (magnitude >= 1) digits = 2;
    return `${value.toFixed(digits)}${unit ? ' ' + unit : ''}`;
}

function formatClock(totalSeconds) {
    const clamped = Math.max(0, Math.min(SECONDS_PER_DAY, Math.round(totalSeconds)));
    const hours = Math.floor(clamped / 3600);
    const minutes = Math.floor((clamped % 3600) / 60);
    const seconds = clamped % 60;
    const pad = (v) => String(v).padStart(2, '0');
    return `${pad(hours)}:${pad(minutes)}:${pad(seconds)}`;
}

function formatClockShort(totalSeconds) {
    return formatClock(totalSeconds).slice(0, 5);
}

function clamp(value, minimum, maximum) {
    return Math.min(Math.max(value, minimum), maximum);
}

function getViewSpan() {
    return state.view.end - state.view.start;
}

// Never let the selection collapse below a few samples, otherwise zooming yields an empty plot.
function getMinimumSpan() {
    return Math.max(state.stepSeconds * 4, 60);
}

function secondsToX(seconds) {
    const { padding, plotWidth } = state.plot;
    return padding.left + ((seconds - state.view.start) / getViewSpan()) * plotWidth;
}

function xToSeconds(x) {
    const { padding, plotWidth } = state.plot;
    return state.view.start + ((x - padding.left) / plotWidth) * getViewSpan();
}

function isSelectionNarrowed() {
    const tolerance = Math.max(state.stepSeconds, 1);
    return (state.cursors.start > state.view.start + tolerance)
        || (state.cursors.end < state.view.end - tolerance);
}

function isZoomed() {
    return (state.view.start > 0) || (state.view.end < SECONDS_PER_DAY);
}

function updateRangeControls() {
    elements.zoomButton.hidden = !isSelectionNarrowed();
    elements.resetButton.disabled = !isZoomed() && !isSelectionNarrowed();
    elements.rangeLabel.textContent = `Showing ${formatClockShort(state.view.start)} to ${formatClockShort(state.view.end)}`;
}

function resetRange() {
    state.view = { start: 0, end: SECONDS_PER_DAY };
    state.cursors = { start: 0, end: SECONDS_PER_DAY };
}

function applyZoom() {
    if (!isSelectionNarrowed()) return;
    state.view = { start: state.cursors.start, end: state.cursors.end };
    state.cursors = { start: state.view.start, end: state.view.end };
    updateRangeControls();
    drawChart();
}

function resetZoom() {
    resetRange();
    updateRangeControls();
    drawChart();
}

function chooseTickStep(spanSeconds) {
    const candidates = [60, 300, 600, 900, 1800, 3600, 7200, 10800, 21600];
    return candidates.find((step) => spanSeconds / step <= 8) || 21600;
}

function formatDayTag(tag) {
    if (!/^\d{6}$/.test(tag)) return tag;
    return `20${tag.slice(0, 2)}-${tag.slice(2, 4)}-${tag.slice(4, 6)}`;
}

function formatBytes(bytes) {
    if (bytes >= 1048576) return `${(bytes / 1048576).toFixed(2)} MB`;
    if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${bytes} B`;
}

function getRequestedType() {
    const params = new URLSearchParams(window.location.search);
    return params.get('type') || '';
}

async function fetchManifest() {
    const response = await fetch('/api/logs', { cache: 'no-store' });
    if (!response.ok) throw new Error(`manifest request failed (${response.status})`);
    return response.json();
}

async function fetchSamples(name) {
    const response = await fetch(`/api/logs/download?name=${encodeURIComponent(name)}`, { cache: 'no-store' });
    if (!response.ok) throw new Error(`log download failed (${response.status})`);
    const buffer = await response.arrayBuffer();
    const count = Math.floor(buffer.byteLength / 4);
    const view = new DataView(buffer);
    const values = new Float32Array(count);
    for (let i = 0; i < count; i++) {
        values[i] = view.getFloat32(i * 4, true);
    }
    return values;
}

function populateChannelSelect(preferredType) {
    elements.channelSelect.innerHTML = state.channels
        .map((channel) => `<option value="${channel.type}">${channel.label}</option>`)
        .join('');

    const match = state.channels.find((channel) => channel.type === preferredType);
    state.channel = match || state.channels[0] || null;
    if (state.channel) elements.channelSelect.value = state.channel.type;
}

function populateDaySelect() {
    const days = state.files
        .filter((file) => file.type === state.channel?.type)
        .sort((a, b) => b.date.localeCompare(a.date));

    elements.daySelect.innerHTML = days
        .map((file) => `<option value="${file.name}">${formatDayTag(file.date)}</option>`)
        .join('');
    elements.daySelect.disabled = days.length === 0;
    state.file = days[0] || null;
    if (state.file) elements.daySelect.value = state.file.name;
}

function updateHeader() {
    const label = state.channel ? state.channel.label : 'Log';
    document.title = `SolarStation - ${label}`;
    elements.title.textContent = label;

    if (state.file) {
        elements.subtitle.textContent = `${formatDayTag(state.file.date)} - ${state.file.name} - ${formatBytes(state.file.bytes)}`;
        elements.download.href = `/api/logs/download?name=${encodeURIComponent(state.file.name)}`;
        elements.download.style.display = '';
    } else if (!state.storageReady) {
        elements.subtitle.textContent = `Log storage unavailable: ${state.storageDiag}`;
        elements.download.style.display = 'none';
    } else if (!state.clockValid) {
        elements.subtitle.textContent = 'Waiting for a valid system clock (NTP). Nothing is recorded until then.';
        elements.download.style.display = 'none';
    } else {
        elements.subtitle.textContent = 'No data recorded yet for this log.';
        elements.download.style.display = 'none';
    }
}

function updateStats() {
    const unit = state.channel ? state.channel.unit : '';
    const values = state.samples;

    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    let count = 0;

    if (values) {
        for (let i = 0; i < values.length; i++) {
            const value = values[i];
            if (!Number.isFinite(value)) continue;
            if (value < min) min = value;
            if (value > max) max = value;
            sum += value;
            count++;
        }
    }

    if (count === 0) {
        elements.statMin.textContent = '--';
        elements.statMax.textContent = '--';
        elements.statAvg.textContent = '--';
        elements.statCount.textContent = '0';
        elements.statPeriod.textContent = '--';
        return;
    }

    elements.statMin.textContent = formatValue(min, unit);
    elements.statMax.textContent = formatValue(max, unit);
    elements.statAvg.textContent = formatValue(sum / count, unit);
    elements.statCount.textContent = String(count);
    elements.statPeriod.textContent = `${state.stepSeconds} s`;
}

function buildColumns(width) {
    const values = state.samples;
    const columns = new Array(width).fill(null);
    const span = getViewSpan();

    for (let i = 0; i < values.length; i++) {
        const value = values[i];
        if (!Number.isFinite(value)) continue;

        const seconds = i * state.stepSeconds;
        if (seconds < state.view.start || seconds > state.view.end) continue;

        const column = clamp(Math.round(((seconds - state.view.start) / span) * (width - 1)), 0, width - 1);
        const bucket = columns[column];
        if (bucket === null) {
            columns[column] = { min: value, max: value, sum: value, count: 1 };
        } else {
            if (value < bucket.min) bucket.min = value;
            if (value > bucket.max) bucket.max = value;
            bucket.sum += value;
            bucket.count++;
        }
    }

    return columns;
}

function drawChart() {
    const canvas = elements.canvas;
    const ratio = window.devicePixelRatio || 1;
    const cssWidth = canvas.clientWidth;
    const cssHeight = canvas.clientHeight;

    canvas.width = Math.round(cssWidth * ratio);
    canvas.height = Math.round(cssHeight * ratio);

    const context = canvas.getContext('2d');
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.clearRect(0, 0, cssWidth, cssHeight);

    const padding = { top: 34, right: 34, bottom: 34, left: 62 };
    const plotWidth = Math.max(1, cssWidth - padding.left - padding.right);
    const plotHeight = Math.max(1, cssHeight - padding.top - padding.bottom);

    // Scale on what the view shows, so zooming into a quiet period actually reveals detail.
    let peak = 0;
    if (state.samples) {
        for (let i = 0; i < state.samples.length; i++) {
            const value = state.samples[i];
            const seconds = i * state.stepSeconds;
            if (seconds < state.view.start || seconds > state.view.end) continue;
            if (Number.isFinite(value) && value > peak) peak = value;
        }
    }
    const yMax = peak > 0 ? peak * Y_AXIS_HEADROOM : 1;

    state.plot = { padding, plotWidth, plotHeight, yMax };

    const toY = (value) => padding.top + plotHeight - (Math.max(0, value) / yMax) * plotHeight;

    context.font = '11px Georgia, "Segoe UI", sans-serif';
    context.fillStyle = '#58685f';
    context.strokeStyle = '#e3d6b8';
    context.lineWidth = 1;

    for (let i = 0; i <= 5; i++) {
        const y = padding.top + (plotHeight * i) / 5;
        context.beginPath();
        context.moveTo(padding.left, y);
        context.lineTo(padding.left + plotWidth, y);
        context.stroke();

        context.textAlign = 'right';
        context.textBaseline = 'middle';
        context.fillText(formatValue(yMax * (1 - i / 5), ''), padding.left - 8, y);
    }

    context.textAlign = 'center';
    context.textBaseline = 'top';
    const tickStep = chooseTickStep(getViewSpan());
    const firstTick = Math.ceil(state.view.start / tickStep) * tickStep;
    for (let tick = firstTick; tick <= state.view.end; tick += tickStep) {
        const x = secondsToX(tick);
        context.beginPath();
        context.moveTo(x, padding.top);
        context.lineTo(x, padding.top + plotHeight);
        context.stroke();
        context.fillText(formatClockShort(tick), x, padding.top + plotHeight + 8);
    }

    context.strokeStyle = '#c3b48f';
    context.beginPath();
    context.moveTo(padding.left, padding.top);
    context.lineTo(padding.left, padding.top + plotHeight);
    context.lineTo(padding.left + plotWidth, padding.top + plotHeight);
    context.stroke();

    const segments = (state.samples && state.samples.length > 0)
        ? buildSegments(Math.round(plotWidth), padding.left)
        : [];

    if (segments.length === 0) {
        context.fillStyle = '#58685f';
        context.textAlign = 'center';
        context.textBaseline = 'middle';
        context.fillText('No samples in this range.', padding.left + plotWidth / 2, padding.top + plotHeight / 2);
    }

    segments.forEach((points) => {
        if (points.length === 1) {
            context.fillStyle = '#2c7a5a';
            context.beginPath();
            context.arc(points[0].x, toY(points[0].bucket.sum), 2.5, 0, Math.PI * 2);
            context.fill();
            return;
        }

        context.fillStyle = 'rgba(44, 122, 90, 0.18)';
        context.beginPath();
        context.moveTo(points[0].x, toY(points[0].bucket.max));
        points.forEach((point) => context.lineTo(point.x, toY(point.bucket.max)));
        for (let i = points.length - 1; i >= 0; i--) {
            context.lineTo(points[i].x, toY(points[i].bucket.min));
        }
        context.closePath();
        context.fill();

        context.strokeStyle = '#2c7a5a';
        context.lineWidth = 1.6;
        context.lineJoin = 'round';
        context.beginPath();
        points.forEach((point, index) => {
            const y = toY(point.bucket.sum / point.bucket.count);
            if (index === 0) context.moveTo(point.x, y);
            else context.lineTo(point.x, y);
        });
        context.stroke();
    });

    drawCursors(context);
}

// Empty columns are real recording gaps, so each run of samples becomes its own segment.
function buildSegments(width, offsetX) {
    const values = state.samples;
    const firstIndex = Math.max(0, Math.floor(state.view.start / state.stepSeconds));
    const lastIndex = Math.min(values.length - 1, Math.ceil(state.view.end / state.stepSeconds));

    if (lastIndex < firstIndex) return [];

    const segments = [];
    let current = null;

    // Past one sample per pixel, bucketing would scatter the curve into isolated points.
    if ((lastIndex - firstIndex + 1) <= width) {
        for (let i = firstIndex; i <= lastIndex; i++) {
            const value = values[i];

            if (!Number.isFinite(value)) {
                current = null;
                continue;
            }

            if (current === null) {
                current = [];
                segments.push(current);
            }

            current.push({
                x: secondsToX(i * state.stepSeconds),
                bucket: { min: value, max: value, sum: value, count: 1 }
            });
        }

        return segments;
    }

    const columns = buildColumns(width);

    for (let i = 0; i < columns.length; i++) {
        if (columns[i]) {
            if (current === null) {
                current = [];
                segments.push(current);
            }
            current.push({ x: offsetX + i, bucket: columns[i] });
        } else {
            current = null;
        }
    }

    return segments;
}

function drawCursors(context) {
    const { padding, plotWidth, plotHeight } = state.plot;
    const top = padding.top;
    const bottom = padding.top + plotHeight;
    const startX = secondsToX(state.cursors.start);
    const endX = secondsToX(state.cursors.end);

    context.save();

    context.fillStyle = 'rgba(32, 48, 40, 0.09)';
    if (startX > padding.left) {
        context.fillRect(padding.left, top, startX - padding.left, plotHeight);
    }
    if (endX < padding.left + plotWidth) {
        context.fillRect(endX, top, padding.left + plotWidth - endX, plotHeight);
    }

    drawCursorHandle(context, startX, top, bottom, state.cursors.start);
    drawCursorHandle(context, endX, top, bottom, state.cursors.end);

    context.restore();
}

function drawCursorHandle(context, x, top, bottom, seconds) {
    const halfWidth = CURSOR_HEAD_WIDTH / 2;
    const headTop = top - CURSOR_HEAD_HEIGHT - CURSOR_TIP_HEIGHT;
    const headBottom = headTop + CURSOR_HEAD_HEIGHT;

    context.strokeStyle = '#cf6a32';
    context.lineWidth = 1;
    context.beginPath();
    context.moveTo(x, top);
    context.lineTo(x, bottom);
    context.stroke();

    context.beginPath();
    context.moveTo(x - halfWidth, headTop);
    context.lineTo(x + halfWidth, headTop);
    context.lineTo(x + halfWidth, headBottom);
    context.lineTo(x + 5, headBottom);
    context.lineTo(x, headBottom + CURSOR_TIP_HEIGHT);
    context.lineTo(x - 5, headBottom);
    context.lineTo(x - halfWidth, headBottom);
    context.closePath();
    context.fillStyle = '#cf6a32';
    context.fill();

    context.fillStyle = '#fff';
    context.font = '11px Georgia, "Segoe UI", sans-serif';
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.fillText(formatClock(seconds), x, headTop + CURSOR_HEAD_HEIGHT / 2);
}

function pickCursor(x) {
    if (!state.plot) return null;

    const startDistance = Math.abs(secondsToX(state.cursors.start) - x);
    const endDistance = Math.abs(secondsToX(state.cursors.end) - x);

    if (Math.min(startDistance, endDistance) > CURSOR_GRAB_RADIUS) return null;
    return startDistance <= endDistance ? 'start' : 'end';
}

function handlePointerDown(event) {
    const target = pickCursor(event.clientX - elements.canvas.getBoundingClientRect().left);
    if (!target) return;

    state.drag = target;
    elements.canvas.setPointerCapture(event.pointerId);
    elements.readout.style.display = 'none';
    event.preventDefault();
}

function handlePointerUp(event) {
    if (!state.drag) return;

    state.drag = null;
    if (elements.canvas.hasPointerCapture(event.pointerId)) {
        elements.canvas.releasePointerCapture(event.pointerId);
    }
}

function handlePointerMove(event) {
    if (!state.plot) return;

    const { padding, plotWidth } = state.plot;
    const bounds = elements.canvas.getBoundingClientRect();
    const x = event.clientX - bounds.left;

    if (state.drag) {
        const minimumSpan = getMinimumSpan();
        const dragged = xToSeconds(x);

        if (state.drag === 'start') {
            state.cursors.start = clamp(dragged, state.view.start, state.cursors.end - minimumSpan);
        } else {
            state.cursors.end = clamp(dragged, state.cursors.start + minimumSpan, state.view.end);
        }

        updateRangeControls();
        drawChart();
        return;
    }

    elements.canvas.style.cursor = pickCursor(x) ? 'ew-resize' : 'crosshair';

    if (!state.samples || state.samples.length === 0 || x < padding.left || x > padding.left + plotWidth) {
        elements.readout.style.display = 'none';
        return;
    }

    const seconds = xToSeconds(x);
    const index = Math.round(seconds / state.stepSeconds);

    if (index < 0 || index >= state.samples.length || !Number.isFinite(state.samples[index])) {
        elements.readout.style.display = 'none';
        return;
    }

    elements.readout.style.display = 'block';
    elements.readout.textContent = `${formatClock(seconds)}  ${formatValue(state.samples[index], state.channel?.unit)}`;
    elements.readout.style.left = `${Math.min(x + 12, bounds.width - elements.readout.offsetWidth - 6)}px`;
    elements.readout.style.top = `${padding.top + 6}px`;
}

async function loadSelectedFile() {
    updateHeader();
    resetRange();

    if (!state.file) {
        state.samples = null;
        updateStats();
        updateRangeControls();
        drawChart();
        return;
    }

    setMessage('Loading samples...');
    try {
        state.samples = await fetchSamples(state.file.name);
        state.stepSeconds = Math.max(state.file.periodMs || 1000, 1) / 1000;
        setMessage('');
    } catch (error) {
        state.samples = null;
        setMessage(error.message);
    }

    updateStats();
    updateRangeControls();
    drawChart();
}

async function initialize() {
    elements.channelSelect.addEventListener('change', () => {
        state.channel = state.channels.find((channel) => channel.type === elements.channelSelect.value) || null;
        populateDaySelect();
        loadSelectedFile();
    });

    elements.daySelect.addEventListener('change', () => {
        state.file = state.files.find((file) => file.name === elements.daySelect.value) || null;
        loadSelectedFile();
    });

    elements.canvas.addEventListener('pointerdown', handlePointerDown);
    elements.canvas.addEventListener('pointermove', handlePointerMove);
    elements.canvas.addEventListener('pointerup', handlePointerUp);
    elements.canvas.addEventListener('pointercancel', handlePointerUp);
    elements.canvas.addEventListener('pointerleave', () => { elements.readout.style.display = 'none'; });
    elements.zoomButton.addEventListener('click', applyZoom);
    elements.resetButton.addEventListener('click', resetZoom);
    window.addEventListener('resize', drawChart);

    updateRangeControls();

    try {
        const manifest = await fetchManifest();
        state.storageReady = manifest.storageReady === true;
        state.clockValid = manifest.clockValid === true;
        state.storageDiag = manifest.storageDiag || 'M95P32 filesystem not mounted';
        state.channels = Array.isArray(manifest.channels) ? manifest.channels : [];
        state.files = Array.isArray(manifest.files) ? manifest.files : [];
    } catch (error) {
        setMessage(error.message);
        drawChart();
        return;
    }

    if (state.channels.length === 0) {
        setMessage('No log channel is registered on this device.');
        drawChart();
        return;
    }

    populateChannelSelect(getRequestedType());
    populateDaySelect();
    await loadSelectedFile();
}

initialize();
