from flask import Blueprint, request, jsonify
from .controller import *

bp = Blueprint('routes', __name__)

@bp.route('/')
def index():
    return "Welcome to the STM32 Developer Dashboard!"

@bp.route('/process_command', methods=['POST'])
def api_process_command():
    """API endpoint to process terminal commands."""
    data = request.json
    command = data.get('command', '')
    timeout_val = data.get('timeout', 0.3)

    if not command:
        return jsonify({'error': 'No command provided'}), 400

    response = process_command(command, timeout_val=timeout_val)
    return jsonify({'response': response})