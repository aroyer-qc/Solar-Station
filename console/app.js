const state = {
    baseUrl: localStorage.getItem('solarstation-base-url') || 'http://192.168.4.1',
    pollTimer: null,
    lastConfig: null,
    lastStatus: null,
    lastLogs: null
};

const scheduleGrid = document.getElementById('scheduleGrid');
const outputCards = document.getElementById('outputCards');
const logsTableBody = document.getElementById('logsTableBody');

function normalizeBaseUrl(url) {
    return url.replace(/\/+$/, '');
}

function setMessage(id, message, ok) {
    const element = document.getElementById(id);
    element.textContent = message;
    element.className = ok ? 'message ok' : 'message';
}

async function request(path, options = {}) {
    const response = await fetch(`${state.baseUrl}${path}`, options);
    const contentType = response.headers.get('content-type') || '';
    const payload = contentType.includes('application/json') ? await response.json() : await response.text();

    if(!response.ok) {
        const message = typeof payload === 'string' ? payload : (payload.message || 'Request failed');
        throw new Error(message);
    }

    return payload;
}

function renderSchedules(outputs) {
    scheduleGrid.innerHTML = outputs.map(output => {
        const schedules = Array.isArray(output.schedules) && output.schedules.length >= 2
            ? output.schedules
            : [
                {
                    slot: 1,
                    enabled: output.enabled,
                    startType: output.startType,
                    startHour: output.startHour,
                    startMinute: output.startMinute,
                    startOffsetMinutes: output.startOffsetMinutes,
                    endType: output.endType,
                    endHour: output.endHour,
                    endMinute: output.endMinute,
                    endOffsetMinutes: output.endOffsetMinutes,
                    dutyPercent: output.dutyPercent
                },
                {
                    slot: 2,
                    enabled: false,
                    startType: 0,
                    startHour: 8,
                    startMinute: 0,
                    startOffsetMinutes: 0,
                    endType: 0,
                    endHour: 18,
                    endMinute: 0,
                    endOffsetMinutes: 0,
                    dutyPercent: 100
                }
            ];

        const renderSlot = (schedule, slot) => {
            const prefix = `output${output.index}Schedule${slot}`;
            return `
                <div class="schedule-slot">
                    <label class="schedule-slot-header"><input type="checkbox" class="schedule-enable" name="${prefix}Enabled" ${schedule.enabled ? 'checked' : ''}>Schedule ${slot} enabled</label>
                    <div class="schedule-slot-body ${schedule.enabled ? '' : 'is-hidden'}">
                        <label for="${prefix}StartType">Start type</label>
                        <select id="${prefix}StartType" name="${prefix}StartType">
                            <option value="0" ${schedule.startType === 0 ? 'selected' : ''}>Fixed time</option>
                            <option value="1" ${schedule.startType === 1 ? 'selected' : ''}>Sunrise</option>
                            <option value="2" ${schedule.startType === 2 ? 'selected' : ''}>Sunset</option>
                        </select>
                        <label for="${prefix}StartHour">Start hour</label>
                        <input type="number" id="${prefix}StartHour" name="${prefix}StartHour" min="0" max="23" value="${schedule.startHour}">
                        <label for="${prefix}StartMinute">Start minute</label>
                        <input type="number" id="${prefix}StartMinute" name="${prefix}StartMinute" min="0" max="59" value="${schedule.startMinute}">
                        <label for="${prefix}StartOffsetMinutes">Start offset minutes</label>
                        <input type="number" id="${prefix}StartOffsetMinutes" name="${prefix}StartOffsetMinutes" min="-720" max="720" value="${schedule.startOffsetMinutes}">
                        <label for="${prefix}EndType">End type</label>
                        <select id="${prefix}EndType" name="${prefix}EndType">
                            <option value="0" ${schedule.endType === 0 ? 'selected' : ''}>Fixed time</option>
                            <option value="1" ${schedule.endType === 1 ? 'selected' : ''}>Sunrise</option>
                            <option value="2" ${schedule.endType === 2 ? 'selected' : ''}>Sunset</option>
                        </select>
                        <label for="${prefix}EndHour">End hour</label>
                        <input type="number" id="${prefix}EndHour" name="${prefix}EndHour" min="0" max="23" value="${schedule.endHour}">
                        <label for="${prefix}EndMinute">End minute</label>
                        <input type="number" id="${prefix}EndMinute" name="${prefix}EndMinute" min="0" max="59" value="${schedule.endMinute}">
                        <label for="${prefix}EndOffsetMinutes">End offset minutes</label>
                        <input type="number" id="${prefix}EndOffsetMinutes" name="${prefix}EndOffsetMinutes" min="-720" max="720" value="${schedule.endOffsetMinutes}">
                        <label for="${prefix}DutyPercent">Duty percent</label>
                        <input type="number" id="${prefix}DutyPercent" name="${prefix}DutyPercent" min="0" max="100" value="${schedule.dutyPercent}">
                    </div>
                </div>
            `;
        };

        return `
            <div class="output-card schedule-output-card">
                <h3>Output ${output.index}</h3>
                <label><input type="checkbox" name="output${output.index}AutomaticMode" ${output.automaticMode ? 'checked' : ''}>Automatic mode default</label>
                <div class="schedule-columns">
                    ${renderSlot(schedules[0], 1)}
                    ${renderSlot(schedules[1], 2)}
                    <div class="schedule-meta">
                        <div class="callout ${output.scheduleConflict ? 'schedule-conflict' : 'muted'}">
                            ${output.scheduleConflict ? 'Conflict detected between Schedule 1 and Schedule 2' : 'No schedule conflict detected'}
                        </div>
                    </div>
                </div>
            </div>
        `;
    }).join('');

    document.querySelectorAll('.schedule-enable').forEach(checkbox => {
        checkbox.addEventListener('change', () => {
            const body = checkbox.closest('.schedule-slot').querySelector('.schedule-slot-body');
            body.classList.toggle('is-hidden', !checkbox.checked);
        });
    });
}

function renderOutputs(statusOutputs, configOutputs) {
    outputCards.innerHTML = statusOutputs.map(output => {
        const config = configOutputs.find(item => item.index === output.index) || {};
        return `
            <div class="output-card">
                <h3>Output ${output.index}</h3>
                <div class="statusline"><span>Mode</span><strong>${output.mode}</strong></div>
                <div class="statusline"><span>Status</span><strong>${output.status}</strong></div>
                <div class="statusline"><span>State</span><strong>${output.state}</strong></div>
                <div class="statusline"><span>PWM</span><strong>${output.pwmPercent}%</strong></div>
                <div class="callout muted">${output.scheduleSummary}</div>
                <label for="outputDuty${output.index}">Manual PWM</label>
                <input type="number" id="outputDuty${output.index}" min="0" max="100" value="${output.pwmPercent}">
                <div class="actions">
                    <button class="action output-action" type="button" data-output="${output.index}" data-action="auto">AUTO</button>
                    <button class="action warning output-action" type="button" data-output="${output.index}" data-action="manual">MANUAL</button>
                    <button class="action output-action" type="button" data-output="${output.index}" data-action="on">ON</button>
                    <button class="action warning output-action" type="button" data-output="${output.index}" data-action="off">OFF</button>
                    <button class="action secondary output-action" type="button" data-output="${output.index}" data-action="toggle">TOGGLE</button>
                    <button class="action secondary pwm-action" type="button" data-output="${output.index}">Apply PWM</button>
                </div>
                <div class="callout muted">Automatic default: ${config.automaticMode ? 'AUTO' : 'MANUAL'}</div>
            </div>
        `;
    }).join('');

    document.querySelectorAll('.output-action').forEach(button => {
        button.addEventListener('click', () => sendOutputAction(button.dataset.output, button.dataset.action));
    });

    document.querySelectorAll('.pwm-action').forEach(button => {
        button.addEventListener('click', () => {
            const output = button.dataset.output;
            const duty = document.getElementById(`outputDuty${output}`).value;
            sendOutputAction(output, 'pwm', duty);
        });
    });
}

function applyConfigToForm(config) {
    state.lastConfig = config;

    const fallbackAzimuthSteps = Number(config.azimuth && config.azimuth.stepsPerDegree) || 0;
    const fallbackElevationSteps = Number(config.elevation && config.elevation.stepsPerDegree) || 0;
    const rawMicrostepMode = Number((config.stepper && config.stepper.microstepMode) || 8);
    const microstepMode = [8, 16, 32, 64].includes(rawMicrostepMode) ? rawMicrostepMode : 8;
    const motor1Steps = Number((config.stepper && config.stepper.motor1StepsPerRevolution) || (config.azimuth && config.azimuth.motorStepsPerRevolution) || ((fallbackAzimuthSteps * 360.0) / (microstepMode || 8))) || 200;
    const motor2Steps = Number((config.stepper && config.stepper.motor2StepsPerRevolution) || (config.elevation && config.elevation.motorStepsPerRevolution) || ((fallbackElevationSteps * 360.0) / (microstepMode || 8))) || 200;
    const motor3Steps = Number((config.stepper && config.stepper.motor3StepsPerRevolution) || 200) || 200;

    document.getElementById('stLatitude').value = config.solarTracking.latitude;
    document.getElementById('stLongitude').value = config.solarTracking.longitude;
    document.getElementById('stAltitude').value = config.solarTracking.altitude;
    document.getElementById('stTimeZoneOffset').value = config.solarTracking.timeZoneOffset;
    document.getElementById('stUseDST').checked = config.solarTracking.useDST;
    document.getElementById('stPressure').value = config.solarTracking.pressure;
    document.getElementById('stTemperature').value = config.solarTracking.temperature;

    document.getElementById('azimuthDegMax').value = config.azimuth.degMax;
    document.getElementById('azimuthDegMin').value = config.azimuth.degMin;
    document.getElementById('azimuthStepSpeedHz').value = config.azimuth.stepSpeedHz;
    document.getElementById('azimuthStepAcceleration').value = config.azimuth.stepAcceleration;
    document.getElementById('azimuthTimeThreshold').value = config.azimuth.timeThreshold;

    document.getElementById('elevationDegMax').value = config.elevation.degMax;
    document.getElementById('elevationDegMin').value = config.elevation.degMin;
    document.getElementById('elevationStepSpeedHz').value = config.elevation.stepSpeedHz;
    document.getElementById('elevationStepAcceleration').value = config.elevation.stepAcceleration;
    document.getElementById('elevationTimeThreshold').value = config.elevation.timeThreshold;

    document.getElementById('stepper1MotorStepsPerRevolution').value = Math.max(1, Math.round(motor1Steps));
    document.getElementById('stepper2MotorStepsPerRevolution').value = Math.max(1, Math.round(motor2Steps));
    document.getElementById('stepper3MotorStepsPerRevolution').value = Math.max(1, Math.round(motor3Steps));
    document.getElementById('stepperMicrostepMode').value = String(microstepMode);

    document.getElementById('ntpServer1').value = config.ntp.server1;
    document.getElementById('ntpServer2').value = config.ntp.server2;
    document.getElementById('ntpServer3').value = config.ntp.server3;
    document.getElementById('wifiSSID').value = config.wifi.ssid;
    document.getElementById('wifiPassword').value = '';

    renderSchedules(config.outputs);
}

function applyStatus(status) {
    state.lastStatus = status;

    document.getElementById('deviceMode').textContent = status.network.mode;
    document.getElementById('deviceIp').textContent = status.network.ip;
    document.getElementById('deviceTime').textContent = status.network.localDateTime;
    document.getElementById('deviceNtp').textContent = status.network.ntpStatus;

    document.getElementById('overviewSunrise').textContent = status.solar.sunrise;
    document.getElementById('overviewSunset').textContent = status.solar.sunset;
    document.getElementById('overviewOverride').textContent = status.solar.trackingOverrideStatus;
    document.getElementById('overviewLoad1').textContent = `${status.sensors.load1CurrentA.toFixed(3)} A`;
    document.getElementById('overviewLoad2').textContent = `${status.sensors.load2CurrentA.toFixed(3)} A`;
    document.getElementById('overviewLimits').textContent = status.signals.limitSwitchSummary;
    document.getElementById('overviewDiag').textContent = status.signals.stepperDiagSummary;
    document.getElementById('overviewOutputs').textContent = `Outputs: ${status.outputs.map(item => `#${item.index} ${item.status} (${item.pwmPercent}%)`).join(' | ')}`;

    document.getElementById('diagOverride').textContent = status.solar.trackingOverrideStatus;
    document.getElementById('diagLimits').textContent = status.signals.limitSwitchSummary;
    document.getElementById('diagSignals').textContent = status.signals.stepperDiagSummary;

    if(state.lastConfig) {
        renderOutputs(status.outputs, state.lastConfig.outputs);
    }
}

async function refreshAll(showMessage = false) {
    try {
        const [config, status] = await Promise.all([
            request('/api/config'),
            request('/api/status')
        ]);

        applyConfigToForm(config);
        applyStatus(status);
        if(showMessage) {
            setMessage('connectionMessage', 'Connected to device.', true);
        }
    }
    catch(error) {
        setMessage('connectionMessage', error.message, false);
    }
}

async function sendOutputAction(output, action, duty) {
    try {
        const body = new URLSearchParams({ output, action });
        if(typeof duty !== 'undefined') {
            body.set('duty', duty);
        }

        const result = await request('/api/output', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });

        setMessage('outputMessage', result.message, true);
        await refreshAll();
    }
    catch(error) {
        setMessage('outputMessage', error.message, false);
    }
}

async function saveConfig(event) {
    event.preventDefault();

    try {
        const form = document.getElementById('configForm');
        const body = new URLSearchParams(new FormData(form));
        const modeUpdates = [];

        if(state.lastConfig && Array.isArray(state.lastConfig.outputs)) {
            state.lastConfig.outputs.forEach(output => {
                const automaticCheckbox = document.querySelector(`input[name="output${output.index}AutomaticMode"]`);
                if(automaticCheckbox) {
                    const action = automaticCheckbox.checked ? 'auto' : 'manual';
                    modeUpdates.push({ output: String(output.index), action });
                }
            });
        }

        const result = await request('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });

        for(const modeUpdate of modeUpdates) {
            await request('/api/output', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: new URLSearchParams(modeUpdate)
            });
        }

        setMessage('configMessage', result.message, true);
        await refreshAll();
    }
    catch(error) {
        setMessage('configMessage', error.message, false);
    }
}

async function applyOverride() {
    try {
        const body = new URLSearchParams({
            azimuth: document.getElementById('overrideAzimuth').value,
            elevation: document.getElementById('overrideElevation').value
        });

        const result = await request('/setTrackingOverride', {
            method: 'POST',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body
        });

        setMessage('diagMessage', result, true);
        await refreshAll();
    }
    catch(error) {
        setMessage('diagMessage', error.message, false);
    }
}

async function cancelOverride() {
    try {
        const result = await request('/cancelTrackingOverride', { method: 'POST' });
        setMessage('diagMessage', result, true);
        await refreshAll();
    }
    catch(error) {
        setMessage('diagMessage', error.message, false);
    }
}

async function restartDevice() {
    try {
        const result = await request('/api/restart', { method: 'POST' });
        setMessage('connectionMessage', result.message, true);
    }
    catch(error) {
        setMessage('connectionMessage', error.message, false);
    }
}

function renderLogs(manifest) {
    state.lastLogs = manifest;

    const usedBytes = Number(manifest.usedBytes || 0);
    const totalBytes = Number(manifest.totalBytes || 0);
    document.getElementById('logsUsage').textContent = `Storage usage: ${usedBytes} / ${totalBytes} bytes`;

    const files = Array.isArray(manifest.files) ? manifest.files.slice() : [];
    files.sort((a, b) => {
        const aDate = (a.date || '000000');
        const bDate = (b.date || '000000');
        if(aDate !== bDate) {
            return aDate < bDate ? 1 : -1;
        }
        return (a.name || '').localeCompare(b.name || '');
    });

    if(files.length === 0) {
        logsTableBody.innerHTML = '<tr><td colspan="5">No log files available.</td></tr>';
        return;
    }

    logsTableBody.innerHTML = files.map(file => {
        const name = String(file.name || '');
        const type = String(file.type || '-');
        const date = String(file.date || '-');
        const bytes = Number(file.bytes || 0);
        const href = `${state.baseUrl}/api/logs/download?name=${encodeURIComponent(name)}`;

        return `
            <tr>
                <td>${name}</td>
                <td>${type}</td>
                <td>${date}</td>
                <td>${bytes}</td>
                <td><a href="${href}" target="_blank" rel="noopener">Download</a></td>
            </tr>
        `;
    }).join('');
}

async function refreshLogs(showMessage = false) {
    try {
        const manifest = await request('/api/logs');
        renderLogs(manifest);
        if(showMessage) {
            setMessage('logsMessage', 'Logs refreshed.', true);
        }
    }
    catch(error) {
        setMessage('logsMessage', error.message, false);
    }
}

function switchTab(targetId) {
    document.querySelectorAll('.tablink').forEach(button => {
        button.classList.toggle('active', button.dataset.tab === targetId);
    });

    document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.toggle('active', tab.id === targetId);
    });
}

function startPolling() {
    if(state.pollTimer !== null) {
        clearInterval(state.pollTimer);
    }

    state.pollTimer = setInterval(() => {
        refreshAll(false);
        refreshLogs(false);
    }, 2000);
}

document.getElementById('baseUrl').value = state.baseUrl;
document.getElementById('connectBtn').addEventListener('click', () => {
    state.baseUrl = normalizeBaseUrl(document.getElementById('baseUrl').value.trim());
    localStorage.setItem('solarstation-base-url', state.baseUrl);
    refreshAll(true);
    refreshLogs(true);
    startPolling();
});
document.getElementById('restartBtn').addEventListener('click', restartDevice);
document.getElementById('refreshLogsBtn').addEventListener('click', () => refreshLogs(true));
document.getElementById('configForm').addEventListener('submit', saveConfig);
document.getElementById('applyOverrideBtn').addEventListener('click', applyOverride);
document.getElementById('cancelOverrideBtn').addEventListener('click', cancelOverride);
document.querySelectorAll('.tablink').forEach(button => button.addEventListener('click', () => switchTab(button.dataset.tab)));

refreshAll(true);
refreshLogs(false);
startPolling();
