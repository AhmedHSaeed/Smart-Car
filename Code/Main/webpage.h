#ifndef WEBPAGE_H
#define WEBPAGE_H

const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>CarBot Control Panel</title>
    <link href="https://fonts.googleapis.com/css2?family=Space+Mono:wght@400;700&family=Poppins:wght@300;400;600;700&display=swap" rel="stylesheet">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        :root {
            --bg-dark: #0a0e27; --bg-secondary: #141829; --bg-tertiary: #1a1f3a;
            --cyan-primary: #00d9ff; --orange-accent: #ff6b35; --orange-dark: #d84315;
            --text-primary: #ffffff; --text-secondary: #b0b8d4;
            --border-color: rgba(0, 217, 255, 0.2);
        }
        html { background: linear-gradient(135deg, var(--bg-dark) 0%, #0f1535 100%); background-attachment: fixed; }
        body {
            font-family: 'Poppins', sans-serif; background: transparent; min-height: 100vh;
            padding: 20px; color: var(--text-primary); overflow-x: hidden;
        }
        body::before {
            content: ''; position: fixed; top: 0; left: 0; width: 100%; height: 100vh;
            background-image: linear-gradient(rgba(0, 217, 255, 0.03) 1px, transparent 1px),
                              linear-gradient(90deg, rgba(0, 217, 255, 0.03) 1px, transparent 1px);
            background-size: 50px 50px; pointer-events: none; z-index: -1;
        }
        .container { max-width: 1200px; margin: 0 auto; animation: fadeInUp 0.8s ease-out; }
        @keyframes fadeInUp { from { opacity: 0; transform: translateY(30px); } to { opacity: 1; transform: translateY(0); } }
        .header { text-align: center; margin-bottom: 40px; padding-bottom: 30px; border-bottom: 2px solid var(--border-color); }
        h1 {
            font-family: 'Space Mono', monospace; font-size: clamp(2rem, 5vw, 3.5rem); font-weight: 700;
            margin-bottom: 10px; background: linear-gradient(135deg, var(--cyan-primary) 0%, #00a8cc 100%);
            -webkit-background-clip: text; -webkit-text-fill-color: transparent;
            text-shadow: 0 0 30px rgba(0, 217, 255, 0.3); letter-spacing: 1px;
        }
        .subtitle { font-size: 0.95rem; color: var(--text-secondary); font-weight: 300; letter-spacing: 2px; text-transform: uppercase; }
        .emergency-container { margin-bottom: 30px; }
        .emergency-stop {
            width: 100%; padding: 18px 30px; background: linear-gradient(135deg, var(--orange-accent) 0%, var(--orange-dark) 100%);
            color: white; border: 2px solid var(--orange-accent); border-radius: 12px; font-size: 1.1rem;
            font-weight: 700; font-family: 'Space Mono', monospace; cursor: pointer; transition: all 0.3s;
            letter-spacing: 1px; box-shadow: 0 0 40px rgba(255, 107, 53, 0.3);
            text-transform: uppercase;
        }
        .emergency-stop:hover { transform: translateY(-3px); box-shadow: 0 0 60px rgba(255, 107, 53, 0.5); filter: brightness(1.1); }
        .emergency-stop:active { transform: translateY(-1px); }
        .main-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 25px; margin-bottom: 30px; }
        .control-section {
            background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px;
            padding: 25px; backdrop-filter: blur(10px); transition: all 0.3s ease;
        }
        .control-section:hover { border-color: rgba(0, 217, 255, 0.4); box-shadow: 0 0 30px rgba(0, 217, 255, 0.1); }
        .section-title {
            font-family: 'Space Mono', monospace; font-size: 0.85rem; font-weight: 700;
            text-transform: uppercase; letter-spacing: 2px; color: var(--cyan-primary);
            margin-bottom: 20px; padding-bottom: 12px; border-bottom: 2px solid var(--border-color);
        }
        .mode-selector { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 12px; }
        .mode-btn {
            padding: 14px 16px; background: var(--bg-tertiary); color: var(--text-secondary);
            border: 2px solid var(--border-color); border-radius: 10px; font-family: 'Poppins', sans-serif;
            font-size: 0.9rem; font-weight: 600; cursor: pointer; transition: all 0.3s;
            text-transform: capitalize; letter-spacing: 0.5px;
        }
        .mode-btn:hover { border-color: var(--cyan-primary); color: var(--cyan-primary); box-shadow: 0 0 20px rgba(0, 217, 255, 0.2); transform: translateY(-2px); }
        .mode-btn.active { background: linear-gradient(135deg, rgba(0, 217, 255, 0.2) 0%, rgba(0, 217, 255, 0.1) 100%); color: var(--cyan-primary); border-color: var(--cyan-primary); box-shadow: 0 0 30px rgba(0, 217, 255, 0.3); }
        .manual-controls { display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px; margin-bottom: 20px; }
        .control-btn {
            padding: 16px; background: var(--bg-tertiary); color: var(--cyan-primary);
            border: 2px solid var(--border-color); border-radius: 12px; font-family: 'Poppins', sans-serif;
            font-size: 0.95rem; font-weight: 600; cursor: pointer; transition: all 0.3s;
            text-transform: uppercase; letter-spacing: 0.5px; position: relative; overflow: hidden;
        }
        .control-btn::before {
            content: ''; position: absolute; top: 50%; left: 50%; width: 0; height: 0;
            background: radial-gradient(circle, rgba(0, 217, 255, 0.3), transparent);
            border-radius: 50%; transform: translate(-50%, -50%); transition: width 0.6s, height 0.6s;
        }
        .control-btn:hover::before { width: 200px; height: 200px; }
        .control-btn:hover { border-color: var(--cyan-primary); box-shadow: 0 0 25px rgba(0, 217, 255, 0.3); transform: translateY(-2px); }
        .control-btn:active { transform: scale(0.95); }
        .speed-control { margin-top: 20px; padding-top: 20px; border-top: 2px solid var(--border-color); }
        .speed-control label { display: block; font-size: 0.85rem; font-weight: 600; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 12px; }
        .speed-input-group { display: flex; gap: 15px; align-items: center; }
        .speed-slider { flex: 1; height: 6px; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 3px; cursor: pointer; -webkit-appearance: none; }
        .speed-slider::-webkit-slider-thumb { -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%; background: linear-gradient(135deg, var(--cyan-primary) 0%, #00a8cc 100%); cursor: pointer; box-shadow: 0 0 15px rgba(0, 217, 255, 0.6); }
        #speedValue { display: inline-block; min-width: 45px; text-align: center; font-family: 'Space Mono', monospace; font-size: 1.1rem; font-weight: 700; color: var(--cyan-primary); }
        .stunt-buttons { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 12px; }
        .stunt-btn {
            padding: 16px; background: linear-gradient(135deg, var(--orange-accent) 0%, rgba(255, 107, 53, 0.8) 100%);
            color: white; border: 2px solid var(--orange-accent); border-radius: 12px;
            font-family: 'Poppins', sans-serif; font-size: 0.9rem; font-weight: 600; cursor: pointer;
            transition: all 0.3s; text-transform: uppercase; letter-spacing: 0.5px;
            box-shadow: 0 0 20px rgba(255, 107, 53, 0.2);
        }
        .stunt-btn:hover { border-color: #ff8c5a; box-shadow: 0 0 35px rgba(255, 107, 53, 0.4); transform: translateY(-2px); filter: brightness(1.1); }
        .stunt-btn:active { transform: scale(0.95); }
        .sensor-display { background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px; padding: 25px; }
        .sensor-row { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 20px; margin-bottom: 25px; }
        .sensor-row:last-child { margin-bottom: 0; }
        .sensor-item { text-align: center; }
        .sensor-label { font-size: 0.75rem; font-weight: 700; color: var(--text-secondary); text-transform: uppercase; letter-spacing: 1px; margin-bottom: 8px; display: block; }
        .sensor-value { font-family: 'Space Mono', monospace; font-size: 1.8rem; font-weight: 700; color: var(--cyan-primary); margin: 8px 0; text-shadow: 0 0 15px rgba(0, 217, 255, 0.4); }
        .sensor-bar { width: 100%; height: 8px; background: var(--bg-tertiary); border: 1px solid var(--border-color); border-radius: 4px; overflow: hidden; }
        .sensor-bar-fill { height: 100%; background: linear-gradient(90deg, var(--cyan-primary) 0%, #00a8cc 100%); transition: width 0.3s; box-shadow: 0 0 15px rgba(0, 217, 255, 0.6); }
        .status-panel { background: var(--bg-secondary); border: 2px solid var(--border-color); border-radius: 16px; padding: 25px; }
        .status-item { display: flex; justify-content: space-between; align-items: center; padding: 12px 0; border-bottom: 1px solid rgba(0, 217, 255, 0.1); }
        .status-item:last-child { border-bottom: none; }
        .status-label { color: var(--text-secondary); font-weight: 600; font-size: 0.9rem; text-transform: uppercase; letter-spacing: 0.5px; }
        .status-value { font-family: 'Space Mono', monospace; color: var(--cyan-primary); font-weight: 700; }
        @media (max-width: 768px) {
            .container { padding: 10px; }
            h1 { font-size: 1.8rem; }
            .main-grid { grid-template-columns: 1fr; }
            .manual-controls { grid-template-columns: repeat(3, 1fr); }
            .mode-selector { grid-template-columns: repeat(2, 1fr); }
            .sensor-row { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>CARBOT</h1>
            <p class="subtitle">Autonomous Vehicle Control System</p>
        </div>
        <div class="emergency-container">
            <button class="emergency-stop" onclick="emergencyStop()">EMERGENCY STOP</button>
        </div>
        <div class="main-grid">
            <div class="control-section">
                <div class="section-title">Control Modes</div>
                <div class="mode-selector">
                    <button class="mode-btn" onclick="selectMode(1)">Manual</button>
                    <button class="mode-btn" onclick="selectMode(2)">Assisted</button>
                    <button class="mode-btn" onclick="selectMode(3)">Autonomous</button>
                    <button class="mode-btn" onclick="selectMode(4)">Stunt</button>
                    <button class="mode-btn" onclick="selectMode(5)">Follow Me</button>
                </div>
            </div>
            <div class="sensor-display">
                <div class="section-title">Sensor Data</div>
                <div class="sensor-row">
                    <div class="sensor-item">
                        <span class="sensor-label">Left (A)</span>
                        <div class="sensor-value" id="sensorA">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barA" style="width: 0%"></div></div>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">Center</span>
                        <div class="sensor-value" id="sensorC">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barC" style="width: 0%"></div></div>
                    </div>
                    <div class="sensor-item">
                        <span class="sensor-label">Right (B)</span>
                        <div class="sensor-value" id="sensorB">--</div>
                        <div class="sensor-bar"><div class="sensor-bar-fill" id="barB" style="width: 0%"></div></div>
                    </div>
                </div>
                <div class="sensor-row">
                    <div class="sensor-item">
                        <span class="sensor-label">Obstacle Angle</span>
                        <div class="sensor-value" id="angle">--</div>
                    </div>
                </div>
            </div>
            <div class="status-panel">
                <div class="section-title">System Status</div>
                <div class="status-item"><span class="status-label">Current Mode:</span><span class="status-value" id="currentMode">Idle</span></div>
                <div class="status-item"><span class="status-label">Motor Command:</span><span class="status-value" id="motorCmd">Stop</span></div>
                <div class="status-item"><span class="status-label">Speed:</span><span class="status-value" id="motorSpeed">0</span></div>
                <div class="status-item"><span class="status-label">Emergency Stop:</span><span class="status-value" id="estopStatus">Normal</span></div>
            </div>
        </div>
        <div id="manualSection" class="control-section" style="display:none;">
            <div class="section-title">Manual Control</div>
            <div class="manual-controls">
                <div></div>
                <button class="control-btn" onmousedown="startMove(1, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(1, currentSpeed)" ontouchend="stopMove()">Forward</button>
                <div></div>
                <button class="control-btn" onmousedown="startMove(3, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(3, currentSpeed)" ontouchend="stopMove()">Left</button>
                <button class="control-btn" onclick="motorCmd(0, 0)">Stop</button>
                <button class="control-btn" onmousedown="startMove(4, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(4, currentSpeed)" ontouchend="stopMove()">Right</button>
                <div></div>
                <button class="control-btn" onmousedown="startMove(2, currentSpeed)" onmouseup="stopMove()" ontouchstart="startMove(2, currentSpeed)" ontouchend="stopMove()">Backward</button>
                <div></div>
            </div>
            <div class="speed-control">
                <label>Speed Control (0-255)</label>
                <div class="speed-input-group">
                    <input type="range" id="speedSlider" min="40" max="255" value="200" class="speed-slider" oninput="updateSpeed(this.value)">
                    <span id="speedValue">200</span>
                </div>
            </div>
        </div>
        <div id="stuntSection" class="control-section" style="display:none;">
            <div class="section-title">Stunt Sequences</div>
            <div class="stunt-buttons">
                <button class="stunt-btn" onclick="startStunt(1)">Spin 360</button>
                <button class="stunt-btn" onclick="startStunt(2)">Zigzag</button>
                <button class="stunt-btn" onclick="startStunt(3)">Figure-8</button>
                <button class="stunt-btn" onclick="startStunt(4)">Brake & Reverse</button>
            </div>
        </div>
    </div>
    <script>
        let currentMode = 0;
        let currentSpeed = 200;
        const modeNames = {0:'Idle',1:'Manual',2:'Assisted',3:'Autonomous',4:'Stunt',5:'Follow Me'};
        const motorCmdNames = {0:'Stop',1:'Forward',2:'Backward',3:'Left',4:'Right',5:'Spin Left',6:'Spin Right'};
        
        function selectMode(mode) {
            currentMode = mode;
            document.querySelectorAll('.mode-btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            document.getElementById('manualSection').style.display = (mode === 1 || mode === 2) ? 'block' : 'none';
            document.getElementById('stuntSection').style.display = (mode === 4) ? 'block' : 'none';
            fetch('/api/setMode?mode=' + mode).catch(e => console.error(e));
        }
        
        function motorCmd(cmd, speed) {
            fetch('/api/motorCmd?cmd=' + cmd + '&speed=' + speed).catch(e => console.error(e));
        }
        
        function startMove(cmd, speed) {
            if (currentMode !== 1 && currentMode !== 2) { alert('Switch to Manual or Assisted mode first!'); return; }
            motorCmd(cmd, speed);
        }
        
        function stopMove() {
            motorCmd(0, 0);
        }
        
        function updateSpeed(val) {
            currentSpeed = parseInt(val);
            document.getElementById('speedValue').textContent = currentSpeed;
        }
        
        function startStunt(stuntId) { fetch('/api/stunt?id=' + stuntId).catch(e => console.error(e)); }
        function emergencyStop() { fetch('/api/emergencyStop').catch(e => console.error(e)); alert('Emergency Stop Activated!'); }
        
        function updateStatus() {
            fetch('/api/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('sensorA').textContent = data.sensorA.toFixed(1) + ' cm';
                    document.getElementById('sensorB').textContent = data.sensorB.toFixed(1) + ' cm';
                    document.getElementById('sensorC').textContent = data.sensorC.toFixed(1) + ' cm';
                    document.getElementById('angle').textContent = data.angle.toFixed(0) + String.fromCharCode(176);
                    const maxDist = 150;
                    document.getElementById('barA').style.width = Math.min(100, (data.sensorA / maxDist) * 100) + '%';
                    document.getElementById('barB').style.width = Math.min(100, (data.sensorB / maxDist) * 100) + '%';
                    document.getElementById('barC').style.width = Math.min(100, (data.sensorC / maxDist) * 100) + '%';
                    document.getElementById('currentMode').textContent = modeNames[data.mode] || 'Unknown';
                    document.getElementById('motorCmd').textContent = motorCmdNames[data.motorCmd] || 'Unknown';
                    document.getElementById('motorSpeed').textContent = data.motorSpeed;
                    document.getElementById('estopStatus').textContent = data.emergencyStop ? 'ACTIVE' : 'Normal';
                }).catch(e => console.error(e));
        }
        
        setInterval(updateStatus, 500);
        updateStatus();
    </script>
</body>
</html>
)rawliteral";

#endif