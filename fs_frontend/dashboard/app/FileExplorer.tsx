'use client'

import React, { useState, useEffect } from 'react';

// Define the shape of a file or directory object
interface FileSystemItem {
  name: string;
  type: 'dir' | 'file';
}

const FileExplorer = () => {
  const [output, setOutput] = useState<string[]>([]);
  const [currentPath, setCurrentPath] = useState("/");
  const [directoryContents, setDirectoryContents] = useState<FileSystemItem[]>([]);
  const [selectedItem, setSelectedItem] = useState<FileSystemItem | null>(null);
  const [contextMenuVisible, setContextMenuVisible] = useState(false);
  const [contextMenuPosition, setContextMenuPosition] = useState({ x: 0, y: 0 });
  const [contextMenuTarget, setContextMenuTarget] = useState<FileSystemItem | null>(null);
  const flaskUrl = "http://127.0.0.1:5000";

  // Fetches the current working directory from the backend using 'pwd'.
  // This function now returns the path string.
  const fetchPwd = async (): Promise<string> => {
    try {
      const response = await fetch(`${flaskUrl}/process_command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ command: "pwd" }),
      });
      const data = await response.json();
      const path = data.response.trim();
      setCurrentPath(path);
      return path;
    } catch (error) {
      console.error("Error fetching current path:", error);
      setOutput((prev) => [...prev, `Error: Failed to fetch current path.`]);
      return "/"; // Return a default path on error
    }
  };

  // Fetches the directory listing from the backend by sending an 'ls' command.
  // It now accepts the path as an argument to ensure it has the latest value.
  const fetchLs = async (path: string) => {
    setOutput([]);
    setSelectedItem(null);
    setContextMenuVisible(false); // Hide context menu when fetching new data
    try {
      const response = await fetch(`${flaskUrl}/process_command`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ command: "ls" }),
      });
      const data = await response.json();
      
      const lines = data.response.split('\n').filter(line => line.trim() !== '');
      const items: FileSystemItem[] = lines.map(line => {
        const name = line.trim();
        // The logic for determining type is based on your assumption: anything without a '.' is a directory.
        const type = name.includes('.') ? 'file' : 'dir';
        return { name, type };
      });
      setDirectoryContents(items);
      setOutput([`Contents of: ${path}`]);

    } catch (error) {
      console.error("Error fetching directory contents:", error);
      setOutput([`Error: Failed to fetch directory contents. Ensure Flask server is running on ${flaskUrl}.`]);
      setDirectoryContents([]);
    }
  };

  useEffect(() => {
    // Fetch path and then list contents upon component mount.
    const initialize = async () => {
      const path = await fetchPwd();
      await fetchLs(path);
    };
    initialize();
  }, []);

  // Handle global clicks to hide the context menu
  useEffect(() => {
    const handleGlobalClick = () => {
      if (contextMenuVisible) {
        setContextMenuVisible(false);
      }
    };
    window.addEventListener('click', handleGlobalClick);
    return () => window.removeEventListener('click', handleGlobalClick);
  }, [contextMenuVisible]);

  // Handle a single click on an item. Navigates on directory, selects on file.
  const handleItemClick = async (item: FileSystemItem) => {
    if (item.type === 'dir') {
      try {
        await fetch(`${flaskUrl}/process_command`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: `cd ${item.name}` }),
        });
        const newPath = await fetchPwd(); // Get the new path from the server
        await fetchLs(newPath); // Get the new directory listing with the updated path
      } catch (error) {
        setOutput((prev) => [...prev, `Error: Failed to change directory.`]);
      }
    } else {
      setSelectedItem(item);
      setOutput([`Selected item: ${item.name}`]);
    }
  };
  
  const handleBackClick = async () => {
    if (currentPath !== '/') {
      try {
        await fetch(`${flaskUrl}/process_command`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: "cd .." }),
        });
        const newPath = await fetchPwd(); // Get the new path from the server
        await fetchLs(newPath); // Get the new directory listing with the updated path
      } catch (error) {
        setOutput((prev) => [...prev, `Error: Failed to go back.`]);
      }
    }
  };

  const handleContextMenu = (e: React.MouseEvent, item: FileSystemItem | null = null) => {
    e.preventDefault();
    setContextMenuTarget(item);
    setContextMenuVisible(true);
    setContextMenuPosition({ x: e.clientX, y: e.clientY });
  };

  const handleCreateDirectory = async () => {
    setContextMenuVisible(false);
    const dirName = prompt("Enter a name for the new directory:");
    if (dirName) {
      setOutput((prev) => [...prev, `> mkdir ${dirName}`]);
      try {
        await fetch(`${flaskUrl}/process_command`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: `mkdir ${dirName}` }),
        });
        setOutput((prev) => [...prev, `Creating directory: ${dirName}`]);
        const path = await fetchPwd();
        await fetchLs(path);
      } catch (error) {
        setOutput((prev) => [...prev, `Error: Failed to communicate with Flask backend.`]);
      }
    }
  };

  const handleRemoveItem = async () => {
    setContextMenuVisible(false);
    if (contextMenuTarget) {
      setOutput((prev) => [...prev, `> rm ${contextMenuTarget.name}`]);
      try {
        await fetch(`${flaskUrl}/process_command`, {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ command: `rm ${contextMenuTarget.name}` }),
        });
        setOutput((prev) => [...prev, `Removing item: ${contextMenuTarget.name}`]);
        const path = await fetchPwd();
        await fetchLs(path);
      } catch (error) {
        setOutput((prev) => [...prev, `Error: Failed to communicate with Flask backend.`]);
      }
    }
  };

  return (
    <div className="flex flex-col h-full bg-gray-900 rounded-lg border border-gray-700 p-4">
      <div className="flex items-center gap-2 mb-4">
        <button
          onClick={handleBackClick}
          disabled={currentPath === '/'}
          className={`bg-gray-700 hover:bg-gray-600 text-white font-bold py-1 px-3 rounded-lg ${currentPath === '/' ? 'opacity-50 cursor-not-allowed' : ''}`}
        >
          &larr; Back
        </button>
        <span className="text-sm text-gray-400">Current Path: <span className="text-white">{currentPath}</span></span>
      </div>
      
      <div
        className="flex-1 overflow-y-auto p-2 bg-gray-800 rounded-lg grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 lg:grid-cols-5 xl:grid-cols-6 gap-4"
        onContextMenu={(e) => handleContextMenu(e)}
      >
        {directoryContents.map((item) => (
          <div
            key={item.name}
            onClick={() => handleItemClick(item)}
            onContextMenu={(e) => {
              e.stopPropagation(); // Prevents the parent container's onContextMenu from firing
              handleContextMenu(e, item);
            }}
            className={`flex flex-col items-center p-2 rounded-lg cursor-pointer transform transition-transform hover:scale-105 ${
              item.type === 'dir' ? 'bg-blue-600' : 'bg-white'
            } ${selectedItem && selectedItem.name === item.name ? 'border-2 border-yellow-400' : ''}`}
            style={{ minHeight: '80px', minWidth: '80px' }}
          >
            <div className="w-12 h-12 flex items-center justify-center">
              {item.type === 'dir' ? (
                <svg className="w-full h-full text-white" fill="currentColor" viewBox="0 0 20 20">
                  <path d="M2 6a2 2 0 012-2h5l2 2h5a2 2 0 012 2v6a2 2 0 01-2 2H4a2 2 0 01-2-2V6z" />
                </svg>
              ) : (
                <svg className="w-full h-full text-gray-800" fill="currentColor" viewBox="0 0 20 20">
                  <path fillRule="evenodd" d="M4 4a2 2 0 012-2h4.586A2 2 0 0113 2.586L16.414 6A2 2 0 0117 7.414V16a2 2 0 01-2 2H6a2 2 0 01-2-2V4zm6 1a1 1 0 011 1v2a1 1 0 102 0V6a1 1 0 112 0v5a1 1 0 11-2 0V9a1 1 0 00-1-1H7a1 1 0 00-1 1v2a1 1 0 11-2 0V6a1 1 0 112 0v2a1 1 0 011-1h1a1 1 0 011 1z" clipRule="evenodd" />
                </svg>
              )}
            </div>
            <span className={`mt-1 text-xs text-center truncate w-full ${item.type === 'dir' ? 'text-white' : 'text-gray-800'}`}>{item.name}</span>
          </div>
        ))}
      </div>
      {contextMenuVisible && (
        <div
          className="absolute bg-gray-700 text-white p-2 rounded-lg shadow-lg z-10"
          style={{ top: contextMenuPosition.y, left: contextMenuPosition.x }}
        >
          {contextMenuTarget ? (
            <button
              onClick={handleRemoveItem}
              className="block w-full text-left p-2 hover:bg-gray-600 rounded"
            >
              Delete
            </button>
          ) : (
            <button
              onClick={handleCreateDirectory}
              className="block w-full text-left p-2 hover:bg-gray-600 rounded"
            >
              New Folder
            </button>
          )}
        </div>
      )}
      <div className="mt-4 p-2 bg-gray-800 rounded-lg max-h-24 overflow-y-auto text-sm text-gray-300">
        {output.map((line, index) => (
          <pre key={index} className="whitespace-pre-wrap">{line}</pre>
        ))}
      </div>
    </div>
  );
};

export default FileExplorer;
