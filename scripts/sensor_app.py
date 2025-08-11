from flask import Flask, request, render_template_string, jsonify
from flask_socketio import SocketIO, emit
from datetime import datetime
import serial
import glob
import asyncio
from bleak import BleakScanner, BleakClient
import threading

RX_UUID = None
TX_UUID = None
TARGET_NAME = "ADAM_BT_TEST"

app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app)

loop = asyncio.new_event_loop()





client = None

message_history = {
    'sensor/lsm6dsl': [],
    'sensor/vl53l0x': [],
    'sensor/hts221': [],
    'sensor/lps22hb': [],
    'sensor/lis3mdl': [],
    'sensor/button0': [],
    'event/step': [],
    'event/tap': []
}
VALID_ENDPOINTS = set(message_history.keys())

async def find_target_device():
    print(f"🔍 Searching for BLE device named '{TARGET_NAME}'...")

    while True:
        devices = await BleakScanner.discover(timeout=5.0)

        for device in devices:
            if device.name == TARGET_NAME:
                print(f"\n✅ Found target device: {device.name} — {device.address}")
                return device

        print("⏳ Target device not found. Retrying...\n")
        await asyncio.sleep(2)  # Delay before retrying

def notification_handler(sender, data):
    if is_ascii_printable(data):
        #print(f"\n📥 Notification from {sender}: {data.decode('ascii')}")
        print(f"{data.decode('ascii')}", end='');
    else:
        #print(f"\n📥 Notification from {sender}: {data}")
        print(f"{data}", end='')

async def run():
    global RX_UUID, TX_UUID, client

    device = await find_target_device()

    print(f"\n🔗 Connecting to {device.name or '(no name)'} — {device.address}")

    client = BleakClient(device)
    try:
        await client.connect()
        if not client.is_connected:
            print("Failed to connect.")
            return

        print("Connected!")

        if client.services is None:
            await client.get_services()

        print("\nGATT Services and Characteristics:")
        for service in client.services:
            print(f"  [Service] {service.uuid}")
            for char in service.characteristics:
                props = ', '.join(char.properties)
                print(f"    [Char] {char.uuid} — Properties: {props}")
                if "notify" in char.properties and RX_UUID is None:
                    RX_UUID = char.uuid
                if ("write" in char.properties or "write-without-response" in char.properties) and TX_UUID is None:
                    TX_UUID = char.uuid

        if not TX_UUID:
            print("⚠️ No writable characteristic found. Cannot proceed.")
            return

        if RX_UUID:
            await client.start_notify(RX_UUID, notification_handler)
            print(f"✅ Subscribed to notifications on {RX_UUID}")
        else:
            print("⚠️ No notify characteristic found.")

        #shell_task = asyncio.create_task(shell(client))
    except:
      print("Error")
      return False
        #try:
            #await shell_task
        #finally:
        #    if RX_UUID:
        #        await client.stop_notify(RX_UUID)



def log_message(endpoint, data):
    timestamp = datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S')
    entry = {"timestamp": timestamp, "data": data}
    message_history[endpoint].append(entry)
    socketio.emit('new_message', {"endpoint": endpoint, "timestamp": timestamp, "data": data})

@app.route('/sensor/<sensor_name>', methods=['POST'])
def sensor_endpoint(sensor_name):
    print(request.data)
    endpoint = f'sensor/{sensor_name}'
    if endpoint not in VALID_ENDPOINTS:
        return jsonify({"error": "Unknown sensor"}), 404
    if not request.is_json:
        return jsonify({"error": "Expected JSON"}), 400
    log_message(endpoint, request.get_json())
    print(request.get_json())
    return jsonify({"status": "ok"}), 200

@app.route('/event/<event_type>', methods=['POST'])
def event_endpoint(event_type):
    endpoint = f'event/{event_type}'
    if endpoint not in VALID_ENDPOINTS:
        return jsonify({"error": "Unknown event"}), 404
    if not request.is_json:
        return jsonify({"error": "Expected JSON"}), 400
    log_message(endpoint, request.get_json())
    return jsonify({"status": "ok"}), 200

@app.route('/dashboard')
def dashboard():
    return render_template_string(DASHBOARD_TEMPLATE, history=message_history)

@app.route('/')
def index():
    return '<h2>Sensor Receiver Running</h2><a href="/dashboard">Dashboard</a>'

@socketio.on('send_serial')
def handle_serial_command(data):
    global RX_UUID, TX_UUID, client, loop
    text = data.get('text', '').strip()
    if not text:
        emit('serial_response', {'status': 'error', 'message': 'No text provided'})
        return

    try:
        ports = glob.glob('/dev/cu.usbmodem*') or glob.glob('/dev/ttyACM*')
        if not ports:
            emit('serial_response', {'status': 'error', 'message': 'No serial device found'})
            return

        port = ports[0]
        if (client != None and client.is_connected):
            #asyncio.run(client.write_gatt_char(TX_UUID, (text + '\n').encode(), response=True))
            print("before async run")
            future = asyncio.run_coroutine_threadsafe(
                client.write_gatt_char(TX_UUID, (text + '\n').encode(), response=False),
                loop
            )
            future.result(timeout=5)
            print("After future called")
        else:
            with serial.Serial(port, 115200, timeout=1) as ser:
                ser.write((text + '\n').encode())
        #client.write_gatt_char(TX_UUID, (text + '\n').encode(), response=True);
        emit('serial_response', {'status': 'ok', 'message': f'Sent to {port}: {text}'})
    except Exception as e:
        emit('serial_response', {'status': 'error', 'message': str(e)})

DASHBOARD_TEMPLATE = '''
<!doctype html>
<html lang="en">
<head>
  <title>Live Sensor Dashboard</title>
  <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css" rel="stylesheet">
  <script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
  <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
</head>
<body class="bg-light">
  <div class="container-fluid my-5">
    <h1 class="mb-4">Live Sensor & Event Dashboard</h1>

    <div class="mb-4">
      <h4>Send Serial Command</h4>
      <div class="input-group mb-3">
        <input type="text" id="serialText" class="form-control" placeholder="Enter command to send to board">
        <button class="btn btn-primary" onclick="sendSerial()">Send</button>
      </div>
      <div class="input-group mb-3">
        <input type="text" id="ipAddrBox" class="form-control" placeholder="Enter IP address (e.g., 192.168.1.100)">
        <input type="text" id="timeBox" class="form-control" placeholder="Enter time interval (ms)">
      </div>
      <div class="mb-3">
        {% for sensor in ['lsm6dsl', 'hts221', 'vl53l0x', 'lis3mdl', 'lps22hb', 'button0'] %}
          <button class="btn btn-success me-2 mb-2" onclick="enableHttp('{{ sensor }}')">Enable HTTP {{ sensor }}</button>
          <button class="btn btn-danger me-2 mb-2" onclick="disableHttp('{{ sensor }}')">Disable HTTP {{ sensor }}</button>
        {% endfor %}
        <button class="btn btn-success me-2 mb-2" onclick="enableTap()">Enable Tap</button>
        <button class="btn btn-danger me-2 mb-2" onclick="disableTap()">Disable Tap</button>
        <button class="btn btn-success me-2 mb-2" onclick="enableStep()">Enable Step</button>
        <button class="btn btn-danger me-2 mb-2" onclick="disableStep()">Disable Step</button>
        <button class="btn btn-danger me-2 mb-2" onclick="sendSerial('wifi_ap')">Enable AP</button>
      </div>
      <div id="serialStatus" class="text-muted"></div>
    </div>

    <div class="row">
      <div class="col-md-3">
        <h4>Accelerometer & Gyroscope</h4>
        <canvas id="chart-lsmdsl"></canvas>
      </div>
      <div class="col-md-3">
        <h4>Distance</h4>
        <canvas id="chart-vl53l0x"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Temperature</h4>
        <canvas id="chart-hts221-temp"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Pressure</h4>
        <canvas id="chart-lps22hb-pressure"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Magnetic Field</h4>
        <canvas id="chart-lis3mdl"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Button State</h4>
        <canvas id="chart-button"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Step Event</h4>
        <canvas id="chart-step"></canvas>
      </div>
      <div class="col-md-3 mt-4">
        <h4>Tap Event</h4>
        <canvas id="chart-tap"></canvas>
      </div>
    </div>

    <hr class="my-5">

    {% for endpoint, logs in history.items() %}
      <div class="card mb-4" id="card-{{ endpoint }}">
        <div class="card-header bg-primary text-white">{{ endpoint }}</div>
        <ul class="list-group list-group-flush" id="log-{{ endpoint|replace('/', '-') }}">
          {% if logs %}
            {% for entry in logs %}
              <li class="list-group-item">
                <strong>{{ entry.timestamp }}:</strong>
                <pre class="mb-0">{{ entry.data | tojson(indent=2) }}</pre>
              </li>
            {% endfor %}
          {% else %}
            <li class="list-group-item text-muted">No messages received yet.</li>
          {% endif %}
        </ul>
      </div>
    {% endfor %}
  </div>

<script>
  const socket = io();

  function createChart(id, label, color, options = {}) {
    return new Chart(document.getElementById(id), {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label: label, data: [], borderColor: color, tension: 0.3
        }]
      },
      options: {
        scales: {
          x: { title: { display: true, text: 'Time' } },
          y: { beginAtZero: true, ...options}
        }
      }
    });
  }

  function createMotionChart(id, title) {
    return new Chart(document.getElementById(id), {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'Accel X', data: [], borderColor: 'red', tension: 0.3 },
          { label: 'Accel Y', data: [], borderColor: 'green', tension: 0.3 },
          { label: 'Accel Z', data: [], borderColor: 'blue', tension: 0.3 },
          { label: 'Gyro X', data: [], borderColor: 'purple', tension: 0.3, borderDash: [5, 5] },
          { label: 'Gyro Y', data: [], borderColor: 'orange', tension: 0.3, borderDash: [5, 5] },
          { label: 'Gyro Z', data: [], borderColor: 'teal', tension: 0.3, borderDash: [5, 5] }
        ]
      },
      options: {
        scales: { 
          x: { title: { display: true, text: 'Time' } }, 
          y: { beginAtZero: false, title: { display: true, text: 'm/s² or °/s' } }
        }
      }
    });
  }

  const accChart = createMotionChart('chart-lsmdsl', 'Accelerometer & Gyroscope');
  const stepChart = createMotionChart('chart-step', 'Step Event');
  const tapChart = createMotionChart('chart-tap', 'Tap Event');
  const distChart = createChart('chart-vl53l0x', 'Distance (mm)', 'purple', {min:-100, max:500});
  //const tempChart = createChart('chart-hts221-temp', 'Temperature (°C)', 'orange', {min: 28, max: 35});
  const pressureChart = createChart('chart-lps22hb-pressure', 'Pressure (hPa)', 'teal');
  const tempChart = new Chart(document.getElementById('chart-hts221-temp'), {
  type: 'line',
  data: {
    labels: [],
    datasets: [
      {
        label: 'Temperature (°C)',
        data: [],
        borderColor: 'orange',
        tension: 0.3,
        yAxisID: 'y-temp'
      },
      {
        label: 'Humidity (%)',
        data: [],
        borderColor: 'blue',
        tension: 0.3,
        yAxisID: 'y-humidity'
      }
    ]
  },
  options: {
    scales: {
      x: {
        title: {
          display: true,
          text: 'Time'
        }
      },
      'y-temp': {
        type: 'linear',
        position: 'left',
        min: -10,
        max: 40,
        title: {
          display: true,
          text: 'Temperature (°C)'
        }
      },
      'y-humidity': {
        type: 'linear',
        position: 'right',
        min: 1,
        max: 100,
        title: {
          display: true,
          text: 'Humidity (%)'
        },
        grid: {
          drawOnChartArea: false  // prevents overlapping grid lines
        }
      }
    }
  }
});
  const magChart = new Chart(document.getElementById('chart-lis3mdl'), {
    type: 'line',
    data: {
      labels: [],
      datasets: [
        { label: 'Mag X', data: [], borderColor: 'red', tension: 0.3 },
        { label: 'Mag Y', data: [], borderColor: 'green', tension: 0.3 },
        { label: 'Mag Z', data: [], borderColor: 'blue', tension: 0.3 }
      ]
    },
    options: {
      scales: { 
        x: { title: { display: true, text: 'Time' } }, 
        y: { beginAtZero: false, title: { display: true, text: 'Gauss' } }
      }
    }
  });
  const buttonChart = createChart('chart-button', 'Button State', 'black', {min: -0.5, max: 1.5});

  function updateChart(chart, label, values) {
    chart.data.labels.push(label);
    chart.data.datasets.forEach((ds, i) => {
      ds.data.push(values[i]);
      if (ds.data.length > 20) ds.data.shift();
    });
    if (chart.data.labels.length > 20) chart.data.labels.shift();
    chart.update();
  }

  socket.on('new_message', msg => {
    const endpoint = msg.endpoint;
    const logId = "log-" + endpoint.replaceAll('/', '-');
    const logList = document.getElementById(logId);
    const item = document.createElement('li');
    item.className = 'list-group-item';
    item.innerHTML = `<strong>${msg.timestamp}:</strong><pre class="mb-0">${JSON.stringify(msg.data, null, 2)}</pre>`;
    const empty = logList.querySelector('.text-muted');
    if (empty) empty.remove();
    logList.appendChild(item);

    if (endpoint === 'sensor/lsm6dsl' && msg.data.accel && msg.data.gyro) {
      updateChart(accChart, msg.timestamp, [...msg.data.accel, ...msg.data.gyro]);
    }
    if (endpoint === 'event/step' && msg.data.accel && msg.data.gyro) {
      updateChart(stepChart, msg.timestamp, [...msg.data.accel, ...msg.data.gyro]);
    }
    if (endpoint === 'event/tap' && msg.data.accel && msg.data.gyro) {
      updateChart(tapChart, msg.timestamp, [...msg.data.accel, ...msg.data.gyro]);
    }
    if (endpoint === 'sensor/vl53l0x' && typeof msg.data.distance === 'number') {
      updateChart(distChart, msg.timestamp, [msg.data.distance]);
    }
    if (endpoint === 'sensor/hts221') {
      updateChart(tempChart, msg.timestamp, [msg.data.temperature, msg.data.humidity]);
    }
    if (endpoint === 'sensor/lps22hb' && typeof msg.data.pressure === 'number') {
      updateChart(pressureChart, msg.timestamp, [msg.data.pressure]);
    }
    if (endpoint === 'sensor/lis3mdl' && msg.data.mag) {
      updateChart(magChart, msg.timestamp, msg.data.mag);
    }
    if (endpoint === 'sensor/button0' && typeof msg.data.button === 'number') {
      updateChart(buttonChart, msg.timestamp, [msg.data.button]);
    }
  });

  function sendSerial(text = "") {
    if (text == "") {
        const cmd = document.getElementById('serialText').value.trim();
	socket.emit('send_serial', {text: cmd})
    } else {
    	socket.emit('send_serial', { text: text });
    }
  }

  function enableHttp(sensor) {
    const ipAddr = document.getElementById('ipAddrBox').value.trim();
    const timeInterval = document.getElementById('timeBox').value.trim();
    if (!ipAddr || !timeInterval) {
      alert('Please enter both IP address and time interval');
      return;
    }
    const endpoint = sensor === 'button0' ? 'sensor/button0' : `sensor/${sensor}`;
    const command = `sensor_timer_http_start ${sensor} ${ipAddr}/${endpoint} ${timeInterval}`;
    sendSerial(command);
  }

  function disableHttp(sensor) {
    const command = `sensor_timer_http_stop ${sensor}`;
    sendSerial(command);
  }

  function enableTap() {
    const ipAddr = document.getElementById('ipAddrBox').value.trim();
    const timeInterval = document.getElementById('timeBox').value.trim();
    if (!ipAddr || !timeInterval) {
      alert('Please enter both IP address and time interval');
      return;
    }
    const command = `lsm6dsl_tap_http_start ${ipAddr}/event/tap ${timeInterval}`;
    sendSerial(command);
  }

  function disableTap() {
    const command = `lsm6dsl_tap_http_stop`;
    sendSerial(command);
  }

  function enableStep() {
    const ipAddr = document.getElementById('ipAddrBox').value.trim();
    const timeInterval = document.getElementById('timeBox').value.trim();
    if (!ipAddr || !timeInterval) {
      alert('Please enter both IP address and time interval');
      return;
    }
    const command = `lsm6dsl_step_http_start ${ipAddr}/event/step ${timeInterval}`;
    sendSerial(command);
  }

  function disableStep() {
    const command = `lsm6dsl_step_http_stop`;
    sendSerial(command);
  }

  socket.on('serial_response', (msg) => {
    const status = document.getElementById('serialStatus');
    status.textContent = msg.message;
    status.className = msg.status === 'ok' ? 'text-success' : 'text-danger';
  });
</script>
</body>
</html>
'''


def ble_loop_runner():
    asyncio.set_event_loop(loop)
    loop.run_until_complete(run())
    loop.run_forever()

if __name__ == '__main__':
    ble_thread = threading.Thread(target=ble_loop_runner, daemon=True)
    ble_thread.start()
    #asyncio.set_event_loop(asyncio.new_event_loop())
    #asyncio.run(run())
    socketio.run(app, host="0.0.0.0", port=80)
