'use client'

import { useEffect, useState, useRef } from "react";

export default function Home() {
  const [command, setCommand] = useState(""); // Stores the current command input
  const [output, setOutput] = useState<string[]>([]); // Stores the terminal output
  const flaskUrl = process.env.NEXT_PUBLIC_FLASK_URL || "http://localhost:5000"; // Flask API URL

  const outputRef = useRef<HTMLDivElement>(null); // Ref to track the output container

  const handleCommandSubmit = async (e: React.FormEvent) => {
    e.preventDefault();

    if (!command.trim()) return;

    // Add the command to the output
    setOutput((prev) => [...prev, `> ${command}`]);

    try {
      // Send the command to the API
      const response = await fetch(`${flaskUrl}/process_command`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify({ command }),
      });

      const data = await response.json();

      // Add the response to the output
      if (data.response) {
        setOutput((prev) => [...prev, data.response]);
      } else {
        setOutput((prev) => [...prev, "Error: No response from server"]);
      }
    } catch (error) {
      setOutput((prev) => [...prev, `Error: ${error}`]);
    }

    // Clear the command input
    setCommand("");
  };

  // Scroll to the bottom of the output whenever it changes
  useEffect(() => {
    if (outputRef.current) {
      outputRef.current.scrollTop = outputRef.current.scrollHeight;
    }
  }, [output]);

  return (
    <div className="flex flex-col items-center justify-center min-h-screen bg-black text-white font-mono p-4">
      <div className="w-2xl h-[400px] border border-gray-700 rounded-lg p-4 bg-gray-900 resize overflow-auto flex flex-col">
        <div ref={outputRef} className="flex-1 h-96 overflow-y-auto mb-4">
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
    </div>
  );
}