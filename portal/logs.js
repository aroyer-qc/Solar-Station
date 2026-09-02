'use strict';

const SECONDS_PER_DAY = 86400;
const Y_AXIS_HEADROOM = 1.25;

const elements = {
    title: document.getElementById('logTitle'),
    subtitle: document.getElementById('logSubtitle'),
    channelSelect: document.getElementById('channelSelect'),
    daySelect: document.getElementById('daySelect'),
    download: document.getElementById('downloadLink'),
    message: document.getElementById('message'),
    canvas: document.getElementById('logChart'),
    readout: document.getElementById('chartReadout'),
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
    startSeconds: 0,
    stepSeconds: 1,
    plot: null,
    deviceNow: null
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

// Device clock, so a day tag can be compared against the logger's local date.
async function fetchDeviceNow() {
    try {
        const response = await fetch('/portal/status', { cache: 'no-store' });
        if (!response.ok) return null;
        const text = await response.text();
        const map = {};
        text.split('\n').forEach((line) => {
            const index = line.indexOf('=');
            if (index > 0) map[line.slice(0, index).trim()] = line.slice(index + 1).trim();
        });
        if (map.timeValid !== '1') return null;
        const match = /^(\d{4})-(\d{2})-(\d{2})\s+(\d{2}):(\d{2}):(\d{2})$/.exec(map.localDateTime || '');
        if (!match) return null;
        return {
            dayTag: `${match[1].slice(2)}${match[2]}${match[3]}`,
            secondsOfDay: Number(match[4]) * 3600 + Number(match[5]) * 60 + Number(match[6])
        };
    } catch (error) {
        return null;
    }
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
    } else {
        elements.subtitle.textContent = 'No recorded data for this log yet.';
        elements.download.style.display = 'none';
    }
}

// Samples carry no timestamp, so the series is anchored on its last point: the device clock for
// today, end of day otherwise. Both assume the logger ran without interruption.
function computeTimeline(count, periodMs) {
    const stepSeconds = Math.max(periodMs, 1) / 1000;
    const isToday = state.deviceNow && state.file && state.deviceNow.dayTag === state.file.date;
    const endSeconds = isToday ? state.deviceNow.secondsOfDay : SECONDS_PER_DAY;
    const startSeconds = Math.max(0, endSeconds - (count - 1) * stepSeconds);
    return { startSeconds, stepSeconds };
}

function updateStats() {
    const unit = state.channel ? state.channel.unit : '';
    const values = state.samples;

    if (!values || values.length === 0) {
        elements.statMin.textContent = '--';
        elements.statMax.textContent = '--';
        elements.statAvg.textContent = '--';
        elements.statCount.textContent = '0';
        elements.statPeriod.textContent = '--';
        return;
    }

    let min = Infinity;
    let max = -Infinity;
    let sum = 0;
    for (let i = 0; i < values.length; i++) {
        const value = values[i];
        if (value < min) min = value;
        if (value > max) max = value;
        sum += value;
    }

    elements.statMin.textContent = formatValue(min, unit);
    elements.statMax.textContent = formatValue(max, unit);
    elements.statAvg.textContent = formatValue(sum / values.length, unit);
    elements.statCount.textContent = String(values.length);
    elements.statPeriod.textContent = `${state.stepSeconds} s`;
}

function buildColumns(width) {
    const values = state.samples;
    const columns = new Array(width).fill(null);

    for (let i = 0; i < values.length; i++) {
        const seconds = state.startSeconds + i * state.stepSeconds;
        const column = Math.min(width - 1, Math.max(0, Math.round((seconds / SECONDS_PER_DAY) * (width - 1))));
        const value = values[i];
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

    const padding = { top: 18, right: 16, bottom: 34, left: 62 };
    const plotWidth = Math.max(1, cssWidth - padding.left - padding.right);
    const plotHeight = Math.max(1, cssHeight - padding.top - padding.bottom);

    let peak = 0;
    if (state.samples) {
        for (let i = 0; i < state.samples.length; i++) {
            if (state.samples[i] > peak) peak = state.samples[i];
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
    for (let hour = 0; hour <= 24; hour += 3) {
        const x = padding.left + (plotWidth * hour) / 24;
        context.beginPath();
        context.moveTo(x, padding.top);
        context.lineTo(x, padding.top + plotHeight);
        context.stroke();
        context.fillText(`${String(hour).padStart(2, '0')}:00`, x, padding.top + plotHeight + 8);
    }

    context.strokeStyle = '#c3b48f';
    context.beginPath();
    context.moveTo(padding.left, padding.top);
    context.lineTo(padding.left, padding.top + plotHeight);
    context.lineTo(padding.left + plotWidth, padding.top + plotHeight);
    context.stroke();

    if (!state.samples || state.samples.length === 0) {
        context.fillStyle = '#58685f';
        context.textAlign = 'center';
        context.textBaseline = 'middle';
        context.fillText('No samples for this day.', padding.left + plotWidth / 2, padding.top + plotHeight / 2);
        return;
    }

    const columns = buildColumns(Math.round(plotWidth));
    const points = [];
    for (let i = 0; i < columns.length; i++) {
        if (columns[i]) points.push({ x: padding.left + i, bucket: columns[i] });
    }
    if (points.length === 0) return;

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
}

function handlePointerMove(event) {
    if (!state.plot || !state.samples || state.samples.length === 0) return;

    const { padding, plotWidth } = state.plot;
    const bounds = elements.canvas.getBoundingClientRect();
    const x = event.clientX - bounds.left;

    if (x < padding.left || x > padding.left + plotWidth) {
        elements.readout.style.display = 'none';
        return;
    }

    const seconds = ((x - padding.left) / plotWidth) * SECONDS_PER_DAY;
    const index = Math.round((seconds - state.startSeconds) / state.stepSeconds);

    if (index < 0 || index >= state.samples.length) {
        elements.readout.style.display = 'none';
        return;
    }

    elements.readout.style.display = 'block';
    elements.readout.textContent = `${formatClock(seconds)}  ${formatValue(state.samples[index], state.channel?.unit)}`;
    elements.readout.style.left = `${Math.min(x + 12, bounds.width - elements.readout.offsetWidth - 6)}px`;
    elements.readout.style.top = `${padding.top}px`;
}

async function loadSelectedFile() {
    updateHeader();

    if (!state.file) {
        state.samples = null;
        updateStats();
        drawChart();
        return;
    }

    setMessage('Loading samples...');
    try {
        state.samples = await fetchSamples(state.file.name);
        const timeline = computeTimeline(state.samples.length, state.file.periodMs || 1000);
        state.startSeconds = timeline.startSeconds;
        state.stepSeconds = timeline.stepSeconds;
        setMessage('');
    } catch (error) {
        state.samples = null;
        setMessage(error.message);
    }

    updateStats();
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

    elements.canvas.addEventListener('mousemove', handlePointerMove);
    elements.canvas.addEventListener('mouseleave', () => { elements.readout.style.display = 'none'; });
    window.addEventListener('resize', drawChart);

    try {
        const [manifest, deviceNow] = await Promise.all([fetchManifest(), fetchDeviceNow()]);
        state.deviceNow = deviceNow;
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
