#!/usr/bin/env python3
"""
Captain Fantastic - Music Server
Simple HTTP server for streaming MP3 files to ESP32 over WiFi

Usage:
  1. Place your MP3 files in a 'music' folder next to this script
  2. Run: python music_server.py
  3. Update MUSIC_SERVER_IP in your ESP32 code to this computer's IP
  4. ESP32 will stream music from http://YOUR_PC_IP:8000/music/
"""

import http.server
import socketserver
import socket
import os

# Configuration
PORT = 8000
MUSIC_DIR = "music"  # Folder containing MP3 files

class MyHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """Custom handler with CORS headers for ESP32 compatibility"""
    
    def end_headers(self):
        # Add CORS headers for cross-origin requests
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET')
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate')
        super().end_headers()
    
    def log_message(self, format, *args):
        # Custom logging format
        print(f"[{self.log_date_time_string()}] {format % args}")

def get_local_ip():
    """Get the local IP address of this computer"""
    try:
        # Create a socket to find local IP (doesn't actually connect)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def main():
    # Check if music directory exists
    if not os.path.exists(MUSIC_DIR):
        print(f"ERROR: Music directory '{MUSIC_DIR}' not found!")
        print(f"Creating directory: {MUSIC_DIR}")
        os.makedirs(MUSIC_DIR)
        print(f"\nPlease copy your MP3 files to: {os.path.abspath(MUSIC_DIR)}")
        print("Then run this script again.")
        return
    
    # Count MP3 files
    mp3_files = [f for f in os.listdir(MUSIC_DIR) if f.lower().endswith('.mp3')]
    
    if len(mp3_files) == 0:
        print(f"WARNING: No MP3 files found in '{MUSIC_DIR}'")
        print(f"Copy your music files to: {os.path.abspath(MUSIC_DIR)}")
        print("\nServer will still start, but ESP32 won't find any songs.\n")
    else:
        print(f"Found {len(mp3_files)} MP3 file(s):")
        for f in sorted(mp3_files):
            size = os.path.getsize(os.path.join(MUSIC_DIR, f)) / 1024
            print(f"  - {f} ({size:.1f} KB)")
        print()
    
    # Get local IP
    local_ip = get_local_ip()
    
    # Start HTTP server
    with socketserver.TCPServer(("", PORT), MyHTTPRequestHandler) as httpd:
        print("=" * 60)
        print("  CAPTAIN FANTASTIC - MUSIC SERVER")
        print("=" * 60)
        print(f"Server running at: http://{local_ip}:{PORT}/")
        print(f"Music directory:   {os.path.abspath(MUSIC_DIR)}")
        print(f"\nESP32 Configuration:")
        print(f"  1. Update MUSIC_SERVER_IP to: \"{local_ip}\"")
        print(f"  2. ESP32 will stream from: http://{local_ip}:{PORT}/music/")
        print(f"\nTest URLs:")
        for f in sorted(mp3_files)[:3]:  # Show first 3 files
            print(f"  http://{local_ip}:{PORT}/music/{f}")
        print("\nPress Ctrl+C to stop server")
        print("=" * 60)
        
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n\nShutting down server...")

if __name__ == "__main__":
    main()
