import serial
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
    ports = glob.glob('/dev/cu.usbmodem*')
    if not ports:
        print("No serial ports found. Ensure the board is connected.")
        sys.exit()
    elif len(ports) > 1:
        print(f"Multiple serial ports have been found. The first one has been selected.")
    return ports[0]

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
            os.system(command[6:])  # Execute the command after 'os_do '
            return f"Executed OS command: {command[6:]}"
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
