#!/usr/bin/env python3
import os
import sys
import socket
import json

def test_socket_connection():
    home = os.environ.get("HOME", "/tmp")
    unix_sock_path = os.path.join(home, ".razor/router.sock")

    print(f"=== Testing RazorAI Router Socket & Model Role Mapping ===")

    # Test small_task prompt -> should map to 'miscode' (mistral/mistral-code-agent-latest)
    if os.path.exists(unix_sock_path):
        print("\n[Sending small_task prompt]")
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client.connect(unix_sock_path)
            req_data = json.dumps({"prompt": "fix syntax error on line 45"})
            client.sendall(req_data.encode('utf-8'))
            resp_data = client.recv(4096).decode('utf-8')
            client.close()

            print("Response from Router:")
            parsed = json.loads(resp_data)
            print(f"  - Session ID: {parsed.get('session_id')}")
            print(f"  - Category: {parsed.get('category')}")
            print(f"  - Assigned Role: {parsed.get('assigned_role')}")
            print(f"  - Model Name: {parsed.get('model_name')}")
            print(f"  - Model ID: {parsed.get('model_id')}")
            print(f"  - Provider: {parsed.get('provider')}")
            print(f"  - Chat JSONL Path: {parsed.get('chat_jsonl')}")

        except Exception as e:
            print("Error:", e)

if __name__ == "__main__":
    test_socket_connection()
