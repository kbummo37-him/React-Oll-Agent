```markdown
# React_Oll: C++ AI Coding Agent with Local LLM (Ollama)

## Features

*   C++ Core Agent: Lightweight and efficient agent logic implemented in C++.
*   Local LLM Integration: Seamlessly connects with local LLMs (e.g., `qwen2.5-coder`) managed by Ollama.
*   Function Calling: Supports a robust function-calling mechanism, allowing the LLM to invoke predefined tools:
    *   `write_file(path: string, content: string)`: Writes specified content to a file.
    *   `read_file(path: string)`: Reads and returns the content of a file.
    *   `run_cmd(command: string)`: Executes a system command and returns its output (e.g., for compiling and running code).
*   Automated Task Execution**: Can interpret complex instructions like "Write a C++ hello world program. Compile and run it." and break them down into tool calls.
*   Extensible: The C++ core can be extended with more tools and logic to support various AI agent functionalities.

### Prerequisites

Before you begin, ensure you have the following installed:

*   C++ Compiler: `g++` (or equivalent).
*   `libcurl` Development Libraries: For HTTP requests within the C++ agent.
*   Ollama: Download and install Ollama from [ollama.ai](https://ollama.ai/).

## Usage

Follow these steps to activate and run your AI agent:

1. Navigate to the Project Directory:
    ```bash
    cd /content/drive/MyDrive/React_Oll
    ```

2. Install System Dependencies:
    Update your package list and install `zstd` (often a dependency for other packages) and `libcurl4-openssl-dev` for C++ HTTP functionality.
    ```bash
    apt-get update && apt-get install -y zstd
    sudo apt update && sudo apt install libcurl4-openssl-dev
    ```

3. Compile the C++ Agent:
    Compile `main.cpp` using `g++`, linking against `libcurl`.
    ```bash
    g++ -o coding_agent main.cpp -lcurl
    ```

4. Install Ollama (if not already installed):
    This command downloads and runs the official Ollama installation script.
    ```bash
    curl -fsSL https://ollama.com/install.sh | sh
    ```

5.  Start the Ollama Server:
    Run the Ollama server in the background. The output (including potential errors) is redirected to `ollama.log`.
    ```bash
    ollama serve > ollama.log 2>&1 &
    ```
    *Note: If you encounter `Error: listen tcp 127.0.0.1:11434: bind: address already in use`, it means Ollama is already running or another process uses that port. You can skip this step or ensure the previous Ollama process is stopped.* 

6.  Pull the Required LLM Model:
    Download the `qwen2.5-coder:3b-instruct` model from Ollama's library. This is the model your agent is configured to use.
    ```bash
    ollama pull qwen2.5-coder:3b-instruct
    ```

7. Run the C++ AI Agent:
    Execute your compiled agent. It will then wait for user input, passing it to the LLM and processing the LLM's responses.
    ```bash
    ./coding_agent
    ```

    The agent will print `user>` prompting for input. Type your commands here.

### Example Interaction (based on `api_prompt.txt`)

Consider the following prompt to the agent:

```
user> Write a C++ hello world program. Compile and run it.
```

The agent (via the LLM) might respond by:
1.  Issuing a `write_file` command to create `main.cpp` with the Hello World code.
2.  Issuing a `run_cmd` command to compile `main.cpp` using `g++`.
3.  Issuing another `run_cmd` command to execute the compiled program (`./a.out` or `./hello_world`).

## Project Structure

*   `main.cpp`: The core C++ source code for the AI agent, handling LLM communication, tool parsing, and execution.
*   `coding_agent`: The compiled executable of the C++ agent.
*   `api_prompt.txt`: Contains the system prompt and example conversation flow that defines the LLM's role, available tools, and expected interaction patterns.
*   `ollama.log`: Log file for the Ollama server, useful for debugging connection or model issues.
*   `test_prompt.txt`: Example prompt template for testing LLM calls.
*   `Makefile` (Optional): Used for automating the build process of the `coding_agent`.
*   `requirements.txt` (Optional): If there are Python dependencies (e.g., for auxiliary scripts not directly part of `main.cpp`).

[Specify your license here, e.g., MIT, Apache 2.0, etc.]
```
