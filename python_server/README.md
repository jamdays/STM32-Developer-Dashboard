## Starting the Server
First make sure you're cd'd into the python_server folder
then run `python3 run.py` in your terminal.
## Example: Running a command
```console
curl -X POST http://127.0.0.1:5000/process_command \
-H "Content-Type: application/json" \
-d '{"command": "term_help"}'
```
Expected output:
```terminal
{
  "response": "Python terminal commands:\n  - 'exit': Exit the terminal\n  - 'help': Get help related to the Discovery Board\n  - 'term_help': Get help related to the Python terminal interface\n  - 'set_timeout <seconds>': Set the timeout for serial commands (default is 0.3 seconds)\n  - 'os_do <command>': Execute a shell command on the host system"
}
```