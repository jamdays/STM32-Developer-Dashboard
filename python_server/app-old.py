from flask import Flask, request
import logging
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from queue import Queue
import threading
import time

log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

app = Flask(__name__)
vector_queue = Queue()

@app.route('/', defaults={'path': ''}, methods=['POST'])
@app.route('/<path:path>', methods=['POST'])
def catch_all(path):
    data = request.get_data(as_text=True)
    if 'accel' in data:
        try:
            accel_line = [line for line in data.split('\n') if 'accel' in line][0]
            accel_line = accel_line.split("accel ")[1]
            parts = accel_line.split(',')
            fx = parts[0].split("x:")[1]
            fy = parts[1].split("y:")[1]
            fz = parts[2].split("z:")[1]
            x = float(fx)
            y = float(fy)
            z = float(fz)
            vector_queue.put((x, y, z))
        except (IndexError, ValueError) as e:
            pass
    return 'OK'

def plot_vectors():
    fig = plt.figure()
    ax = fig.add_subplot(111, projection='3d')
    
    # Set up the plot
    ax.set_xlabel('X')
    ax.set_ylabel('Y')
    ax.set_zlabel('Z')
    ax.set_title('3D Acceleration Vector')
    
    # Initialize quiver objects and previous vector
    quiverX, quiverY, quiverZ = None, None, None
    last_x, last_y, last_z = 0, 0, 0
    
    # Set fixed plot limits
    ax.set_xlim([-15, 15])
    ax.set_ylim([-15, 15])
    ax.set_zlim([-15, 15])
    
    while True:
        try:
            x, y, z = vector_queue.get(timeout=1) # Wait for a new vector
            
            # Animation settings
            animation_seconds = 0.45
            frames = 30
            
            # Create a smooth transition
            for i in range(frames + 1):
                # Interpolate between the last and new vector
                inter_x = last_x + (x - last_x) * (i / frames)
                inter_y = last_y + (y - last_y) * (i / frames)
                inter_z = last_z + (z - last_z) * (i / frames)
                
                magnitude = (inter_x**2 + inter_y**2 + inter_z**2)**0.5
                
                # Clear the previous vector
                if quiverX:
                    quiverX.remove()
                if quiverY:
                    quiverY.remove()
                if quiverZ:
                    quiverZ.remove()
                
                # Draw the new vector
                quiverX = ax.quiver(0, 0, 0, inter_x, 0, 0, color='red')
                quiverY = ax.quiver(0, 0, 0, 0, inter_y, 0, color='green')
                quiverZ = ax.quiver(0, 0, 0, 0, 0, inter_z, color='blue')
                
                # Update title
                ax.set_title(f'3D Acceleration Vector (Magnitude: {magnitude:.2f})')
                
                plt.draw()
                plt.pause(animation_seconds / frames)
            
            # Update the last vector
            last_x, last_y, last_z = x, y, z
            
        except Exception as e:
            # This allows the plot to stay open and responsive
            plt.pause(0.01)

if __name__ == '__main__':
    # Start the Flask app in a separate thread
    flask_thread = threading.Thread(target=lambda: app.run(host='0.0.0.0', port=80, debug=False))
    flask_thread.daemon = True
    flask_thread.start()

    # Run the plotting function on the main thread
    plot_vectors()
