const tabs = document.querySelectorAll('.tablink');
const panels = document.querySelectorAll('.tabcontent');
const wifiDhcpToggle = document.getElementById('wifiUseDHCP');
const staticNetworkInputs = document.querySelectorAll('[data-static-network]');
const staticNetworkGroups = document.querySelectorAll('[data-static-network-group]');
const liveFields = document.querySelectorAll('[data-live-key]');
const stepper1MotorStepsPerRevolutionInput = document.getElementById('stepper1MotorStepsPerRevolution');
const stepper2MotorStepsPerRevolutionInput = document.getElementById('stepper2MotorStepsPerRevolution');
const stepperMicrostepModeInput = document.getElementById('stepperMicrostepMode');
const azimuthGearReductionInput = document.getElementById('azimuthGearReduction');
const elevationGearReductionInput = document.getElementById('elevationGearReduction');
const azimuthStepsPerDegreeComputedInput = document.getElementById('azimuthStepsPerDegreeComputed');
const elevationStepsPerDegreeComputedInput = document.getElementById('elevationStepsPerDegreeComputed');
const ACTIVE_TAB_KEY = 'solarstation-active-tab';
const REFRESH_INTERVAL_MS = 2000;

function parsePositiveNumber(value, fallback) {
    const parsed = Number(value);
    return Number.isFinite(parsed) && parsed > 0 ? parsed : fallback;
}

function computeStepsPerDegree(motorStepsPerRevolution, microstepMode, reductionRatio) {
    return (motorStepsPerRevolution * microstepMode * reductionRatio) / 360.0;
}

function refreshComputedStepsPerDegree() {
    const motor1 = parsePositiveNumber(stepper1MotorStepsPerRevolutionInput?.value, 200);
    const motor2 = parsePositiveNumber(stepper2MotorStepsPerRevolutionInput?.value, 200);
    const microstep = parsePositiveNumber(stepperMicrostepModeInput?.value, 8);
    const azimuthReduction = parsePositiveNumber(azimuthGearReductionInput?.value, 1);
    const elevationReduction = parsePositiveNumber(elevationGearReductionInput?.value, 1);
    if (azimuthStepsPerDegreeComputedInput) {
        azimuthStepsPerDegreeComputedInput.value = computeStepsPerDegree(motor1, microstep, azimuthReduction).toFixed(3);
    }
    if (elevationStepsPerDegreeComputedInput) {
        elevationStepsPerDegreeComputedInput.value = computeStepsPerDegree(motor2, microstep, elevationReduction).toFixed(3);
    }
}

function getModeValue(outputIndex) {
    const field = document.querySelector(`[data-live-key="out${outputIndex}Mode"]`);
    return field ? field.textContent.trim().toUpperCase() : '';
}

function syncManualControls(outputIndex) {
    const toggle = document.querySelector(`[data-output-manual-toggle="${outputIndex}"]`);
    if (!toggle) return;
    document.querySelectorAll(`[data-manual-action="${outputIndex}"]`).forEach((button) => {
        button.disabled = !toggle.checked;
    });
}

function syncOutputStateButtons(outputIndex) {
    const field = document.querySelector(`[data-live-key="out${outputIndex}State"]`);
    if (!field) return;
    const isOn = field.textContent.trim().toUpperCase() === 'ON';
    document.querySelector(`[data-manual-action="${outputIndex}"][value="on"]`)?.classList.toggle('is-current-state', isOn);
    document.querySelector(`[data-manual-action="${outputIndex}"][value="off"]`)?.classList.toggle('is-current-state', !isOn);
}

async function sendOutputMode(outputIndex, isManual) {
    const payload = new URLSearchParams({ output: String(outputIndex), action: isManual ? 'manual' : 'auto' });
    const response = await fetch('/setOutputControl', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: payload });
    if (!response.ok) throw new Error('Failed to update output mode');
}

async function sendOutputAction(form, submitter) {
    const formData = new FormData(form);
    if (submitter?.name) formData.set(submitter.name, submitter.value);
    const payload = new URLSearchParams();
    formData.forEach((value, key) => payload.set(key, String(value)));
    const response = await fetch('/setOutputControl', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: payload });
    if (!response.ok) throw new Error('Failed to update output control');
}

function initializeOutputActionForms() {
    document.querySelectorAll('#outputs form[action="/setOutputControl"]').forEach((form) => {
        form.addEventListener('submit', async (event) => {
            event.preventDefault();
            try { await sendOutputAction(form, event.submitter || null); await refreshLiveData(); }
            catch (_) { alert('Failed to update output control.'); }
        });
    });
}

function initializeOutputControlsFromMode() {
    [1, 2, 3].forEach((outputIndex) => {
        const toggle = document.querySelector(`[data-output-manual-toggle="${outputIndex}"]`);
        if (!toggle) return;
        toggle.checked = getModeValue(outputIndex) === 'MANUAL';
        syncManualControls(outputIndex);
        syncOutputStateButtons(outputIndex);
        toggle.addEventListener('change', async () => {
            const previous = getModeValue(outputIndex) === 'MANUAL';
            syncManualControls(outputIndex);
            try { await sendOutputMode(outputIndex, toggle.checked); await refreshLiveData(); }
            catch (_) { toggle.checked = previous; syncManualControls(outputIndex); alert('Failed to update mode.'); }
        });
    });
}

function setScheduleConfigurationState(scheduleOutput, slot, isEnabled) {
    const configuration = scheduleOutput.querySelector(`[data-schedule-config="${slot}"]`);
    if (!configuration) return;
    configuration.hidden = !isEnabled;
    configuration.querySelectorAll('input, select').forEach((input) => { input.disabled = !isEnabled; });
}

function syncScheduleControls(scheduleOutput) {
    const schedule1 = scheduleOutput.querySelector('[data-schedule-enabled="1"]');
    const schedule2 = scheduleOutput.querySelector('[data-schedule-enabled="2"]');
    if (!schedule1 || !schedule2) return;
    setScheduleConfigurationState(scheduleOutput, 1, schedule1.checked);
    setScheduleConfigurationState(scheduleOutput, 2, schedule2.checked);
}

function initializeScheduleControls() {
    document.querySelectorAll('.schedule-output').forEach((scheduleOutput) => {
        const schedule1 = scheduleOutput.querySelector('[data-schedule-enabled="1"]');
        const schedule2 = scheduleOutput.querySelector('[data-schedule-enabled="2"]');
        if (!schedule1 || !schedule2) return;
        schedule1.addEventListener('change', () => syncScheduleControls(scheduleOutput));
        schedule2.addEventListener('change', () => syncScheduleControls(scheduleOutput));
        syncScheduleControls(scheduleOutput);
    });
}

function initializeScheduleDaySelectors() {
    const weekdays = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
    document.querySelectorAll('[data-schedule-days]').forEach((container) => {
        const prefix = container.dataset.schedulePrefix;
        const activeDaysMask = Number(container.dataset.scheduleDays) || 0;
        if (!prefix) return;
        weekdays.forEach((weekday, index) => {
            const input = document.createElement('input');
            input.type = 'checkbox'; input.name = `${prefix}Day${index}`; input.checked = (activeDaysMask & (1 << index)) !== 0;
            const label = document.createElement('label'); label.append(input, weekday); container.append(label);
        });
    });
}

function updateStaticNetworkInputs() {
    if (!wifiDhcpToggle) return;
    staticNetworkInputs.forEach((input) => { input.disabled = wifiDhcpToggle.checked; });
    staticNetworkGroups.forEach((group) => { group.hidden = wifiDhcpToggle.checked; });
}

function parseDegreesFromSummary(value) {
    const match = typeof value === 'string' ? value.match(/-?\d+(?:\.\d+)?/) : null;
    return match ? Number(match[0]) : Number.NaN;
}

function normalizeDegreeLabel(value) {
    if (typeof value !== 'string') return value;
    return value.replace(/\s*\bdeg\b/gi, ' \u00b0').replace(/\s+/g, ' ').trim();
}

function getConfiguredElevationMin() {
    const minInfo = document.getElementById('calcElevMinInfo');
    if (!minInfo) return Number.NaN;
    return parseDegreesFromSummary(minInfo.dataset.elevationMinValue || minInfo.textContent || '');
}

function updateElevationMinimumIndicator() {
    const minInfo = document.getElementById('calcElevMinInfo');
    if (!minInfo) return;
    const configuredMin = getConfiguredElevationMin();
    if (!Number.isFinite(configuredMin)) return;
    minInfo.textContent = `Min: ${configuredMin.toFixed(1)} \u00b0`;
}

function setRangeClass(field, inRange) {
    if (!field) return;
    field.classList.remove('range-ok', 'range-bad');
    if (inRange === true) field.classList.add('range-ok');
    if (inRange === false) field.classList.add('range-bad');
}

function updateCalculatedRangeIndicator() {
    const az = document.querySelector('[data-live-key="calcAz"]');
    const el = document.querySelector('[data-live-key="calcElev"]');
    const min = document.querySelector('[data-live-key="todayAzMin"]');
    const max = document.querySelector('[data-live-key="todayAzMax"]');
    const elMax = document.querySelector('[data-live-key="todayElevMax"]');
    const azimuth = parseDegreesFromSummary(az?.textContent || '');
    const elevation = parseDegreesFromSummary(el?.textContent || '');
    const minAzimuth = parseDegreesFromSummary(min?.textContent || '');
    const maxAzimuth = parseDegreesFromSummary(max?.textContent || '');
    const maxElevation = parseDegreesFromSummary(elMax?.textContent || '');
    const minElevation = getConfiguredElevationMin();
    setRangeClass(az, Number.isFinite(azimuth) && Number.isFinite(minAzimuth) && Number.isFinite(maxAzimuth) && azimuth >= minAzimuth && azimuth <= maxAzimuth);
    setRangeClass(el, Number.isFinite(elevation) && Number.isFinite(minElevation) && Number.isFinite(maxElevation) && elevation >= minElevation && elevation <= maxElevation);
}

function updateMpptLinkLed() {
    const led = document.getElementById('mpptLinkLed');
    const field = document.querySelector('[data-live-key="mpptLinkStatus"]');
    if (!led || !field) return;
    const status = field.textContent.trim().toUpperCase();
    led.classList.remove('led-waiting', 'led-ok', 'led-error');
    led.classList.add(status === 'OK' ? 'led-ok' : (['ERROR', 'TIMEOUT', 'CRC'].includes(status) ? 'led-error' : 'led-waiting'));
}

function applyLiveData(data) {
    liveFields.forEach((field) => {
        const key = field.dataset.liveKey;
        if (!(key && key in data)) return;
        const value = String(data[key]);
        field.textContent = normalizeDegreeLabel(value);
    });
    updateMpptLinkLed(); updateCalculatedRangeIndicator();
    [1, 2, 3].forEach((outputIndex) => {
        const toggle = document.querySelector(`[data-output-manual-toggle="${outputIndex}"]`);
        if (!toggle) return;
        toggle.checked = getModeValue(outputIndex) === 'MANUAL'; syncManualControls(outputIndex); syncOutputStateButtons(outputIndex);
    });
}

async function refreshLiveData() {
    try {
        const response = await fetch('/portal/status', { cache: 'no-store' });
        if (!response.ok) { console.warn('Portal status request failed:', response.status); return; }
        const data = {};
        (await response.text()).split('\n').forEach((line) => { const i = line.indexOf('='); if (i > 0) data[line.substring(0, i).trim()] = line.substring(i + 1).trim(); });
        applyLiveData(data);
    } catch (error) { console.warn('Portal status refresh failed:', error); }
}

function restartSystem() {
    if (!confirm('Restart SolarStation now?')) return;
    fetch('/restartSystem', { method: 'POST' }).then((response) => {
        if (!response.ok) throw new Error('Restart failed');
        alert('Restart requested. The device should come back shortly.');
    }).catch(() => alert('Failed to request restart.'));
}

if (wifiDhcpToggle) { wifiDhcpToggle.addEventListener('change', updateStaticNetworkInputs); updateStaticNetworkInputs(); }
function activateTab(tabId) {
    tabs.forEach((item) => item.classList.toggle('active', item.dataset.tab === tabId));
    panels.forEach((panel) => panel.classList.toggle('active', panel.id === tabId));
    localStorage.setItem(ACTIVE_TAB_KEY, tabId);
}

function getTabFromPath(pathname) {
    if (pathname === '/portal/network' || pathname === '/portal/network/') return 'network';
    if (pathname === '/portal/outputs' || pathname === '/portal/outputs/') return 'outputs';
    if (pathname === '/portal/config' || pathname === '/portal/config/') return 'config';
    if (pathname === '/portal/network.html') return 'network';
    if (pathname === '/portal/outputs.html') return 'outputs';
    if (pathname === '/portal/config.html') return 'config';
    return '';
}

const tabFromPath = getTabFromPath(window.location.pathname);
const savedTab = localStorage.getItem(ACTIVE_TAB_KEY);
if (tabFromPath && document.getElementById(tabFromPath)) {
    activateTab(tabFromPath);
} else if (savedTab && document.getElementById(savedTab)) {
    activateTab(savedTab);
} else if (document.getElementById('config')) {
    activateTab('config');
}
initializeOutputControlsFromMode(); initializeOutputActionForms(); initializeScheduleDaySelectors(); initializeScheduleControls();
[stepper1MotorStepsPerRevolutionInput, stepper2MotorStepsPerRevolutionInput, stepperMicrostepModeInput, azimuthGearReductionInput, elevationGearReductionInput].forEach((input) => { if (input) { input.addEventListener('input', refreshComputedStepsPerDegree); input.addEventListener('change', refreshComputedStepsPerDegree); } });
refreshComputedStepsPerDegree(); updateElevationMinimumIndicator(); updateMpptLinkLed(); updateCalculatedRangeIndicator();
async function scheduleLiveDataRefresh() { await refreshLiveData(); window.setTimeout(scheduleLiveDataRefresh, REFRESH_INTERVAL_MS); }
scheduleLiveDataRefresh();
