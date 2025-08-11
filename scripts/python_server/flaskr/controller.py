import subprocess
import serial
import serial.tools.list_ports # For Windows and cross-platform listing
import platform
import csv
import os
import sys
import time
import glob
import re

opening = r"""
 ____ ____ ____ ____ ____ _________ ____ ____ ____ 
||S |||T |||M |||3 |||2 |||       |||D |||e |||v ||
||__|||__|||__|||__|||__|||_______|||__|||__|||__||
|/__\|/__\|/__\|/__\|/__\|/_______\|/__\|/__\|/__\|
 ____ ____ ____ ____ ____ ____ ____ ____ ____      
||D |||a |||s |||h |||b |||o |||a |||r |||d ||     
||__|||__|||__|||__|||__|||__|||__|||__|||__||     
|/__\|/__\|/__\|/__\|/__\|/__\|/__\|/__\|/__\|            
Welcome to the STM32 Developer Dashboard!
This is a Python terminal interface for the STM32 Discovery Board.
Type 'term_help' for help related to the Python terminal interface.
Type 'help' for help related to the Discovery Board.                                                                                    
        """

def get_opening():
    return opening

# Configuration
def find_serial_port():
    if platform.system() == "Windows":
        # Windows-specific serial port detection
        ports = list(serial.tools.list_ports.comports())
        
        likely_microcontroller_ports = [
            p for p in ports if (
                "USB Serial Device" in str(p.description) or
                "Arduino" in str(p.description) or
                "CP210x" in str(p.description) or
                "CH340" in str(p.description) or
                "FT232R" in str(p.description) or
                "VID:PID" in str(p.hwid)
            )
        ]

        if not likely_microcontroller_ports:
            print("No serial ports found. Ensure the board is connected.")
            sys.exit()
        elif len(likely_microcontroller_ports) > 1:
            print(f"Multiple potential serial ports found: {[p.device for p in likely_microcontroller_ports]}. The first one has been selected.")
        
        return likely_microcontroller_ports[0].device
        
    elif platform.system() in ["Linux", "Darwin"]: # Darwin is macOS
        # Unix-like (macOS/Linux) serial port detection
        # Common patterns for microcontrollers on these systems
        patterns = [
            '/dev/ttyUSB*',   # Linux (e.g., ESP32, Arduino using CP210x/CH340)
            '/dev/ttyACM*',   # Linux (e.g., Arduino Uno/Mega using native USB)
            '/dev/cu.usbmodem*', # macOS (common for Arduino, ESP32 Dev Boards)
            '/dev/tty.usbmodem*', # macOS (alternative for some USB modems)
        ]
        
        found_ports = []
        for pattern in patterns:
            found_ports.extend(glob.glob(pattern))
        
        # Remove duplicates
        found_ports = sorted(list(set(found_ports)))

        if not found_ports:
            print("No serial ports found. Ensure the board is connected.")
            sys.exit()
        elif len(found_ports) > 1:
            print(f"Multiple serial ports found: {found_ports}. The first one has been selected.")
        
        return found_ports[0]
    else:
        print(f"Unsupported operating system: {platform.system()}")
        sys.exit()



# SERIAL_PORT = '/dev/cu.usbmodem103'  # Adjust for your macOS port (e.g., /dev/cu.usbmodemXXXX)
SERIAL_PORT = find_serial_port()
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


def filter_line(line, command_sent):
    ANSI_ESCAPE = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    filter = ['<dbg>', '<wrn>', 'uart:~$']
    if any(f in line for f in filter):
        return None
    if command_sent.strip() == line.strip():
        return None
    return ANSI_ESCAPE.sub('', line)

def send_command(command, timeout_val=0.3):
    try:
        with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=timeout_val) as ser:
            command += '\n'  # Ensure the command ends with a newline

            # Flush input and output buffers
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            # Send the shell command
            ser.write(command.encode('utf-8'))
            print(f"Sent command: {command.strip()}")

            # Read and print response
            print("Board output:")
            start_time = time.time()
            response = ""
            while time.time() - start_time < timeout_val:  # read for timeout seconds
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                line = filter_line(line, command)
                if line:
                    response += line + "\n"
                    print(line)
            return response

    except serial.SerialException as e:
        print(f"Serial error: {e}")

def process_command(command, timeout_val=0.3):
    """Process a single command and return the response."""
    if command.lower() == 'term_help':
        return (
            "Python terminal commands:\n"
            "  - 'help': Get help related to the Discovery Board\n"
            "  - 'term_help': Get help related to the Python terminal interface\n"
            "  - 'set_timeout <seconds>': Set the timeout for serial commands (default is 0.3 seconds)\n"
            "  - 'os_do <command>': Execute a shell command on the host system\n"
            "  - 'clear': Clear the terminal output\n"
        )
    elif command.lower().startswith('set_timeout '):
        try:
            timeout_val = float(command.split()[1])
            return f"Timeout set to {timeout_val} seconds"
        except (IndexError, ValueError):
            return "Invalid timeout value. Usage: set_timeout <seconds>"
    elif command.lower().startswith('os_do '):
        try:
            result = subprocess.run(command[6:], shell=True, text=True, capture_output=True)
            if result.returncode == 0:
                return result.stdout.strip()  # Return the command's output
            else:
                return f"Error executing OS command: {result.stderr.strip()}"
        except Exception as e:
            return f"Error executing OS command: {e}"
    else:
        # Send the command to the board
        response = send_command(command, timeout_val=timeout_val)
        return response

def terminal():
    timeout_val = 0.3  # Default timeout value
    print(opening)
    print(f"Using serial port: {SERIAL_PORT}")
    while True:
        print("Ready to send commands. Type 'exit' to quit, or 'term_help' for help related to the Python terminal interface.")
        command = input("Enter command: ").strip()
        response = process_command(command, timeout_val=timeout_val)
        if response == "Exiting...":
            print(response)
            break
        if response.startswith("Timeout set to"):
            timeout_val = float(response.split()[-2])
        print(response)


if __name__ == '__main__':
    try:
        terminal()
    except KeyboardInterrupt:
        print("Program terminated")
