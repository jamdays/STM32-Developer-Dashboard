import serial
import csv
import os
import time
import numpy as np
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from micromlgen import port

# Configuration
SERIAL_PORT = '/dev/ttyACM0'  # Adjust for your macOS port (e.g., /dev/cu.usbmodemXXXX)
BAUD_RATE = 115200
DATA_DIR = 'motion_data'
GESTURES = ['up-down', 'shake', 'wave', 'circle', 'tap', 'twist', 'figure-eight']
SAMPLES_PER_GESTURE = 50
SAMPLE_DURATION = 2  # seconds (128 samples at 52 Hz)
FEATURES = [
    'mean_ax', 'mean_ay', 'mean_az', 'std_ax', 'std_ay', 'std_az', 'sma_a',
    'corr_ax_ay', 'corr_ax_az', 'corr_ay_az',
    'mean_gx', 'mean_gy', 'mean_gz', 'std_gx', 'std_gy', 'std_gz', 'sma_g',
    'corr_gx_gy', 'corr_gx_gz', 'corr_gy_gz'
]

def collect_data(gesture):
    """Collect accelerometer and gyroscope data for a specific gesture."""
    os.makedirs(DATA_DIR, exist_ok=True)
    filename = f"{DATA_DIR}/{gesture}_{int(time.time())}.csv"
    print(f"Collecting data for {gesture}. Move the board now...")
    
    data = []
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port {SERIAL_PORT}: {e}")
        print("Check if the board is connected and the port is correct.")
        return None, gesture

    start_time = time.time()
    
    while time.time() - start_time < SAMPLE_DURATION:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                try:
                    timestamp, ax, ay, az, gx, gy, gz = map(float, line.split(','))
                    data.append([ax, ay, az, gx, gy, gz])
                except ValueError:
                    print(f"Invalid data format: {line}")
                    continue
        except UnicodeDecodeError as e:
            print(f"UnicodeDecodeError: {e}. Skipping invalid byte sequence.")
            continue
    
    ser.close()
    
    if len(data) < 10:  # Minimum number of samples
        print(f"Insufficient data collected ({len(data)} samples). Check UART output.")
        return None, gesture
    
    # Compute features
    data = np.array(data)
    ax, ay, az, gx, gy, gz = data[:, 0], data[:, 1], data[:, 2], data[:, 3], data[:, 4], data[:, 5]
    try:
        features = [
            np.mean(ax), np.mean(ay), np.mean(az),
            np.std(ax), np.std(ay), np.std(az),
            np.mean(np.abs(ax) + np.abs(ay) + np.abs(az)),
            np.corrcoef(ax, ay)[0, 1] if len(ax) > 1 else 0.0,
            np.corrcoef(ax, az)[0, 1] if len(ax) > 1 else 0.0,
            np.corrcoef(ay, az)[0, 1] if len(ay) > 1 else 0.0,
            np.mean(gx), np.mean(gy), np.mean(gz),
            np.std(gx), np.std(gy), np.std(gz),
            np.mean(np.abs(gx) + np.abs(gy) + np.abs(gz)),
            np.corrcoef(gx, gy)[0, 1] if len(gx) > 1 else 0.0,
            np.corrcoef(gx, gz)[0, 1] if len(gx) > 1 else 0.0,
            np.corrcoef(gy, gz)[0, 1] if len(gy) > 1 else 0.0
        ]
    except Exception as e:
        print(f"Error computing features: {e}")
        return None, gesture
    
    # Save raw data
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'ax', 'ay', 'az', 'gx', 'gy', 'gz'])
        for i, row in enumerate(data):
            writer.writerow([start_time + i * 0.019] + list(row))
    
    print(f"Data saved to {filename}")
    return features, gesture

def train_model(data, labels):
    """Train a Random Forest classifier."""
    clf = RandomForestClassifier(n_estimators=20, max_depth=10, random_state=42)
    clf.fit(data, labels)
    return clf

def export_model(clf):
    """Export model to C code, ensuring C compatibility."""
    c_code = port(clf, classmap={g: i for i, g in enumerate(GESTURES)}, platform='c')
    # Replace <cstdarg> with <stdarg.h> in the generated code
    c_code = c_code.replace('#include <cstdarg>', '#include <stdarg.h>')
    with open('model.h', 'w') as f:
        f.write(c_code)
    with open('model.c', 'w') as f:
        f.write('#include "model.h"\n' + c_code)
    print("Model exported to model.h and model.c")

def main():
    # Collect data
    all_features = []
    all_labels = []
    
    for gesture in GESTURES:
        for i in range(SAMPLES_PER_GESTURE):
            input(f"Prepare to collect {gesture} sample {i+1}/{SAMPLES_PER_GESTURE}. Press Enter to start...")
            features, label = collect_data(gesture)
            if features is None:
                print(f"Skipping {gesture} sample {i+1} due to collection error.")
                continue
            all_features.append(features)
            all_labels.append(label)
    
    if not all_features:
        print("No valid data collected. Exiting.")
        return
    
    # Train model
    clf = train_model(np.array(all_features), all_labels)
    
    # Evaluate model
    from sklearn.metrics import accuracy_score
    predictions = clf.predict(all_features)
    print(f"Training accuracy: {accuracy_score(all_labels, predictions):.2f}")
    
    # Export model to C
    export_model(clf)
    
    # Monitor inference results
    print("Switch to inference mode by pressing the button on the board.")
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        while True:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(line)
    except serial.SerialException as e:
        print(f"Error opening serial port for inference: {e}")

if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print("Program terminated")
