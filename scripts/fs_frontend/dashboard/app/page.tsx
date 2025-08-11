'use client'

import React, { useEffect, useState, useRef, PropsWithChildren } from "react";
import FileExplorer from './FileExplorer'; // Import the new FileExplorer component

// An ErrorBoundary component to catch errors in child components
class ErrorBoundary extends React.Component<PropsWithChildren<{}>> {
  state = { hasError: false, error: null };

  static getDerivedStateFromError(error: Error) {
    return { hasError: true, error };
  }

  render() {
    if (this.state.hasError) {
      return (
        <div className="text-red-500 p-4 bg-red-100 rounded-lg border border-red-400">
          <h2 className="text-xl font-bold">Something went wrong.</h2>
          <p>{this.state.error?.toString()}</p>
          <pre className="mt-2 text-sm">{this.state.error?.stack}</pre>
        </div>
      );
    }
    return this.props.children;
  }
}

// Sub-component for the main dashboard view
const Dashboard = ({ setActiveView, onCommandSubmit }) => {
  return (
    <div className="flex flex-col h-full p-4">
      <h2 className="text-xl font-bold mb-4 text-center">Developer Dashboard</h2>
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {/* Button to navigate to the sensor menu */}
        <button
          onClick={() => setActiveView("sensor-menu")}
          className="bg-purple-600 hover:bg-purple-700 text-white font-bold py-4 px-6 rounded-lg shadow-md transition-transform transform hover:scale-105"
        >
          Read Sensors
        </button>

        {/* Example of another command button */}
        <button
          onClick={() => onCommandSubmit("ping")}
          className="bg-green-600 hover:bg-green-700 text-white font-bold py-4 px-6 rounded-lg shadow-md transition-transform transform hover:scale-105"
        >
          Ping Server
        </button>

        {/* Another example command button */}
        <button
          onClick={() => onCommandSubmit("clear")}
          className="bg-red-600 hover:bg-red-700 text-white font-bold py-4 px-6 rounded-lg shadow-md transition-transform transform hover:scale-105"
        >
          Clear Output
        </button>
      </div>
    </div>
  );
};

// Sub-component for the sensor menu
const SensorMenu = ({ setActiveView, onCommandSubmit }) => {
  // Array of sensor data based on the provided C struct
  const sensors = [
    { name: "hts221", label: "hts221" },
    { name: "lps22hb", label: "lps22hb" },
    { name: "lis3mdl", label: "lis3mdl" },
    { name: "lsm6dsl", label: "lsm6dsl" },
    { name: "vl53l0x", label: "vl53l0x" },
    { name: "button0", label: "Button (button0)" },
  ];

  const handleSensorRead = (sensor: string) => {
    // This will now send a command like "read hts221"
    onCommandSubmit(`read ${sensor}`);
  };

  return (
    <div className="flex flex-col h-full p-4">
      <div className="flex justify-start mb-4">
        <button
          onClick={() => setActiveView("dashboard")}
          className="bg-gray-600 hover:bg-gray-700 text-white font-bold py-2 px-4 rounded-lg shadow-md transition-transform transform hover:scale-105"
        >
          &larr; Back to Dashboard
        </button>
      </div>
      <h2 className="text-xl font-bold mb-4 text-center">Select a Sensor to Read</h2>
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {sensors.map((sensor) => (
          <button
            key={sensor.name}
            onClick={() => handleSensorRead(sensor.name)}
            className="bg-blue-600 hover:bg-blue-700 text-white font-bold py-4 px-6 rounded-lg shadow-md transition-transform transform hover:scale-105"
          >
            {sensor.label}
          </button>
        ))}
      </div>
    </div>
  );
};

// Terminal tab component (your original code)
const TerminalTab = ({ onCommandLocal, command, setCommand, output, outputRef, handleCommandSubmit }) => {
  return (
    <div className="flex flex-col h-full">
      <div ref={outputRef} className="flex-1 h-96 overflow-y-auto mb-4 p-4 bg-gray-900 rounded-lg border border-gray-700">
        {output.map((line, index) => (
          <pre key={index} className="whitespace-pre-wrap">
            {line}
          </pre>
        ))}
      </div>
      <form onSubmit={handleCommandSubmit} className="flex">
        <input
          type="text"
          className="flex-1 bg-black text-white border border-gray-700 rounded-l-lg p-2 focus:outline-none"
          placeholder="Type a command..."
          value={command}
          onChange={(e) => setCommand(e.target.value)}
        />
        <button
          type="submit"
          className="bg-blue-600 hover:bg-blue-700 text-white px-4 py-2 rounded-r-lg"
        >
          Send
        </button>
      </form>
    </div>
  );
};

// Main Home component
export default function Home() {
  const [command, setCommand] = useState("");
  const [output, setOutput] = useState<string[]>([]);
  const [activeTab, setActiveTab] = useState("terminal"); // New state for active tab
  const [guiView, setGuiView] = useState("dashboard"); // New state for the GUI sub-view
  const flaskUrl = "http://127.0.0.1:5000";

  const outputRef = useRef<HTMLDivElement>(null);
  const localCommands = ["clear", "ping"]; // 'read' is now handled by the server

  const startupScreen = async () => {
    try {
      const response = await fetch(`${flaskUrl}/startup`);
      if (!response.ok) {
        throw new Error("Failed to fetch startup screen");
      }
      const data = await response.json();
      const responseArray = data.response.split("\n").map((line) => line.trim());
      setOutput(responseArray);
    } catch (error) {
      console.error("Error fetching startup screen:", error);
      setOutput((prev) => [...prev, `Error: Failed to connect to Flask backend. Please ensure it is running on ${flaskUrl}.`]);
    }
  };

  useEffect(() => {
    startupScreen();
  }, []);

  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  const handleCommandLocal = (command: string) => {
    if (!command.trim()) return;

    // Check for "clear"
    if (command === "clear") {
      setOutput([]);
      setCommand("");
      return;
    }
    
    // Check for "ping"
    if (command === "ping") {
      setTimeout(() => {
        setOutput((prev) => [...prev, "pong"]);
      }, 100);
      setCommand("");
      return;
    }
  };

  const handleCommandSubmit = async (e: React.FormEvent | string) => {
    let commandToExecute = "";

    if (typeof e === 'string') {
      commandToExecute = e;
    } else {
      e.preventDefault();
      commandToExecute = command;
    }

    if (!commandToExecute.trim()) return;

    const isLocal = localCommands.some(cmd => commandToExecute.startsWith(cmd));
    if (isLocal) {
      handleCommandLocal(commandToExecute);
      // Clear the input only if the command was from the terminal input
      if (typeof e !== 'string') {
        setCommand("");
      }
      return;
    }

    setOutput((prev) => [...prev, `> ${commandToExecute}`]);

    try {
      const response = await fetch(`${flaskUrl}/process_command`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ command: commandToExecute }),
      });

      const data = await response.json();

      if (data.response) {
        setOutput((prev) => [...prev, data.response]);
      } else {
        setOutput((prev) => [...prev, "Error: No response from server"]);
      }
    } catch (error) {
      setOutput((prev) => [...prev, `Error: ${error}`]);
    }

    // Clear the input only if the command was from the terminal input
    if (typeof e !== 'string') {
      setCommand("");
    }
  };

  return (
    <ErrorBoundary>
      <div className="flex flex-col items-center justify-center min-h-screen bg-black text-white font-mono p-4">
        <nav className="w-full max-w-2xl mb-4 bg-gray-900 rounded-lg border border-gray-700 flex">
          <button
            onClick={() => {
              setActiveTab("terminal");
              setGuiView("dashboard"); // Reset GUI view on tab switch
            }}
            className={`flex-1 py-2 px-4 rounded-l-lg transition-colors ${
              activeTab === "terminal" ? "bg-gray-800 text-white font-bold" : "text-gray-400 hover:bg-gray-800"
            }`}
          >
            Terminal
          </button>
          <button
            onClick={() => setActiveTab("gui")}
            className={`flex-1 py-2 px-4 transition-colors ${
              activeTab === "gui" ? "bg-gray-800 text-white font-bold" : "text-gray-400 hover:bg-gray-800"
            }`}
          >
            GUI
          </button>
          <button
            onClick={() => setActiveTab("file-explorer")}
            className={`flex-1 py-2 px-4 rounded-r-lg transition-colors ${
              activeTab === "file-explorer" ? "bg-gray-800 text-white font-bold" : "text-gray-400 hover:bg-gray-800"
            }`}
          >
            File Explorer
          </button>
        </nav>

        <div className="w-full max-w-2xl h-[500px] flex flex-col">
          {activeTab === "terminal" && (
            <TerminalTab
              onCommandLocal={handleCommandLocal}
              command={command}
              setCommand={setCommand}
              output={output}
              outputRef={outputRef}
              handleCommandSubmit={handleCommandSubmit}
            />
          )}
          {activeTab === "gui" && (
            <div className="h-full bg-gray-900 rounded-lg border border-gray-700 flex flex-col">
              <div className="flex-1 overflow-y-auto p-4">
                {/* Output area for the GUI */}
                <h2 className="text-xl font-bold mb-2">GUI Output</h2>
                <div ref={outputRef} className="h-full overflow-y-auto">
                  {output.map((line, index) => (
                    <pre key={index} className="whitespace-pre-wrap">{line}</pre>
                  ))}
                </div>
              </div>
              <div className="p-4 border-t border-gray-700">
                {guiView === "dashboard" && <Dashboard setActiveView={setGuiView} onCommandSubmit={handleCommandSubmit} />}
                {guiView === "sensor-menu" && <SensorMenu setActiveView={setGuiView} onCommandSubmit={handleCommandSubmit} />}
              </div>
            </div>
          )}
          {activeTab === "file-explorer" && <FileExplorer />}
        </div>
      </div>
    </ErrorBoundary>
  );
}