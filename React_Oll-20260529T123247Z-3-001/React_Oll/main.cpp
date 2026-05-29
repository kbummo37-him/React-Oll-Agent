#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Platform Abstraction Layer */
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define AGENT_MKDIR(path) _mkdir(path)
#define AGENT_POPEN _popen
#define AGENT_PCLOSE _pclose
/* Manual definitions for Windows exit status parsing */
#undef WIFEXITED
#undef WEXITSTATUS
#define WIFEXITED(s) ((s) != -1)
#define WEXITSTATUS(s) (s)
#else
#include <sys/wait.h>
#include <unistd.h>
#define AGENT_MKDIR(path) mkdir(path, 0755)
#define AGENT_POPEN popen
#define AGENT_PCLOSE pclose
#endif

const unsigned int MAX_CONTEXT_LEN = 32 * 1024;
const unsigned int MAX_LLM_RESPONSE_LEN = 8192;
const unsigned int MAX_TOOL_RESPONSE_LEN = 8192;

/* Forward declarations for all utility functions to ensure they can be called from anywhere */
int IsSafeRelativePath(const char* path);
int BuildWorkspacePath(const char* path, char* full_path, unsigned int full_path_len);
int WriteOllamaPromptFile(const char* context, char* prompt_path, size_t prompt_path_len);
const char* GetOllamaModel();
int EnsureWorkspace();

const unsigned int MAX_USER_PROMPT_LEN = 4096;
const char AGENT_WORKSPACE[] = "agent_workspace";
const char OLLAMA_MODEL[] = "qwen2.5-coder:3b-instruct";
const char OLLAMA_PROMPT_FILE[] = "ollama_prompt.txt";

const char system_prompt[] =
    "<|im_start|>system\n\n"\
    "You have access to the following functions. To call a function, output a JSON object on its own line:\n"\
    "{\"name\": \"function_name\", \"arguments\": {\"arg1\": \"value1\", \"arg2\": \"value2\"}}\n\n"\
    "Call only one function at a time. If a tool call fails, analyze the error and figure out which tool to call next to fix the issue\n"\
    "Your goal is to reach the user's objective without conversational filler.\n"\
    "IMPORTANT: All file operations must be relative paths within the 'agent_workspace' directory. Do not use absolute paths.\n"\
    "Available functions:\n"\
    "- write_file(path: string, content: string): Writes content to a specified file path\n"\
    "- read_file(path: string): Reads and returns the content of a file\n"\
    "- run_cmd(command: string): Executes a system command and returns the output\n"\
    "<|im_end|>\n";

const char user_prompt_template[] =
    "<|im_start|>user\n"\
    "%s<|im_end|>\n"\
    "<|im_start|>assistant\n";

const char assistant_end[] = "<|im_end|>\n";

const char non_thinking_tags[] = "<think>\n\n</think>\n\n";

const char tool_response_template[] =
    "<|im_end|>\n"\
    "<|im_start|>user\n"\
    "<tool_response>\n"\
    "%s\n"\
    "</tool_response><|im_end|>\n"\
    "<|im_start|>assistant\n";

struct AgentContext
{
    char* text;
    unsigned int max_len;
    unsigned int committed_len;
    int assistant_message_open;
};

struct ToolCall
{
    char name[64];
    char path[512];
    char content[16384];
    char command[1024];
};

int AppendText(AgentContext* ctx, const char* text)
{
    size_t current_len = strlen(ctx->text);
    size_t remaining = ctx->max_len - current_len;
    size_t text_len = strlen(text);

    if (remaining == 0 || text_len >= remaining)
    {
        return -1;
    }

    strncat(ctx->text, text, remaining - 1);
    return 0;
}

int AppendFormat(AgentContext* ctx, const char* format, ...)
{
    int written;
    size_t current_len = strlen(ctx->text);
    size_t remaining = ctx->max_len - current_len;
    va_list args;

    if (remaining == 0)
    {
        return -1;
    }

    va_start(args, format);
    written = vsnprintf(&ctx->text[current_len], remaining, format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= remaining)
    {
        return -1;
    }

    return 0;
}

void AddAssistantPrefill(AgentContext* ctx)
{
    ctx->committed_len = (unsigned int)strlen(ctx->text);
    AppendText(ctx, non_thinking_tags);
    ctx->assistant_message_open = 1;
}

int BeginSession(AgentContext* ctx, char* buffer, unsigned int buffer_len)
{
    ctx->text = buffer;
    ctx->max_len = buffer_len;
    ctx->committed_len = 0;
    ctx->assistant_message_open = 0;
    ctx->text[0] = '\0';

    if (AppendText(ctx, system_prompt) != 0)
    {
        return -1;
    }

    ctx->committed_len = (unsigned int)strlen(ctx->text);
    return 0;
}

int AppendUserPrompt(AgentContext* ctx, const char* user_prompt)
{
    ctx->text[ctx->committed_len] = '\0';

    if (ctx->assistant_message_open)
    {
        if (AppendText(ctx, assistant_end) != 0)
        {
            return -1;
        }
        ctx->assistant_message_open = 0;
    }

    if (AppendFormat(ctx, user_prompt_template, user_prompt) != 0)
    {
        return -1;
    }

    AddAssistantPrefill(ctx);
    return 0;
}

int AppendAssistantResponse(AgentContext* ctx, const char* assistant_response)
{
    ctx->text[ctx->committed_len] = '\0';

    if (AppendText(ctx, assistant_response) != 0)
    {
        return -1;
    }

    ctx->committed_len = (unsigned int)strlen(ctx->text);
    ctx->assistant_message_open = 1;
    return 0;
}

int AppendToolResponse(AgentContext* ctx, const char* tool_response)
{
    if (AppendFormat(ctx, tool_response_template, tool_response) != 0)
    {
        return -1;
    }

    AddAssistantPrefill(ctx);
    return 0;
}

const char* GetOllamaModel()
{
    const char* model = getenv("OLLAMA_MODEL");
    return model ? model : OLLAMA_MODEL;
}

int WriteOllamaPromptFile(const char* context, char* prompt_path, size_t prompt_path_len)
{
    char full_path[1024];
    if (BuildWorkspacePath(OLLAMA_PROMPT_FILE, full_path, sizeof(full_path)) != 0)
    {
        return -1;
    }
    if (snprintf(prompt_path, prompt_path_len, "%s", full_path) >= (int)prompt_path_len)
    {
        return -1;
    }

    FILE* f = fopen(full_path, "wb");
    if (!f)
    {
        return -1;
    }

    fwrite(context, 1, strlen(context), f);
    fclose(f);
    return 0;
}









int CallLLM(const char* context, char* llm_response, unsigned int llm_response_len)
{
    char command[MAX_CONTEXT_LEN + 2048];
    FILE* f = fopen("api_prompt.txt", "w");
    if (f) {
        fprintf(f, "%s", context);
        fclose(f);
    }

    snprintf(command, sizeof(command),
        "jq -n --rawfile p api_prompt.txt '{model: \"%s\", prompt: $p, stream: false}' | "
        "curl -s -X POST http://localhost:11434/api/generate -d @- | jq -r .response",
        GetOllamaModel());

    // fprintf(stderr, "DEBUG: Executing command: %s\n", command); //
    
    FILE* pipe = AGENT_POPEN(command, "r");
    if (!pipe) return -1;

    size_t total = 0;
    while (total + 1 < llm_response_len) {
        size_t n = fread(llm_response + total, 1, llm_response_len - total - 1, pipe);
        if (n == 0) break;
        total += n;
    }
    llm_response[total] = '\0';
    AGENT_PCLOSE(pipe);
    return 0;
}


int EnsureWorkspace()
{
    if (AGENT_MKDIR(AGENT_WORKSPACE) == 0)
    {
        return 0;
    }

    if (errno == EEXIST)
    {
        return 0;
    }

    return -1;
}

int IsSafeRelativePath(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return 0;
    }

    /* Block absolute paths (starts with /, \, or has a drive letter like C:) and traversal */
    if (path[0] == '/' || path[0] == '\\' || (path[0] != '\0' && path[1] == ':') || strstr(path, "..") != NULL)
    {
        return 0;
    }

    return 1;
}

int BuildWorkspacePath(const char* path, char* full_path, unsigned int full_path_len)
{
    if (!IsSafeRelativePath(path))
    {
        return -1;
    }

    int written = snprintf(full_path, full_path_len, "%s/%s", AGENT_WORKSPACE, path);
    if (written < 0 || (unsigned int)written >= full_path_len)
    {
        return -1;
    }

    return 0;
}

void JsonSkipWhitespace(const char* text, size_t* pos)
{
    while (text[*pos] == ' ' || text[*pos] == '\n' || text[*pos] == '\r' || text[*pos] == '\t')
    {
        (*pos)++;
    }
}

int JsonReadString(const char* text, size_t* pos, char* out, unsigned int out_len)
{
    unsigned int out_pos = 0;

    if (text[*pos] != '"')
    {
        return -1;
    }

    (*pos)++;
    while (text[*pos] && text[*pos] != '"')
    {
        char ch = text[*pos];
        if (ch == '\\')
        {
            (*pos)++;
            ch = text[*pos];
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch == '"' || ch == '\\' || ch == '/') {}
            else return -1;
        }

        if (out_pos + 1 >= out_len)
        {
            return -1;
        }

        out[out_pos++] = ch;
        (*pos)++;
    }

    if (text[*pos] != '"')
    {
        return -1;
    }

    (*pos)++;
    out[out_pos] = '\0';
    return 0;
}

const char* FindJsonObject(const char* text)
{
    return strchr(text, '{');
}

int ExtractJsonStringValue(const char* json, const char* key, char* out, unsigned int out_len)
{
    char pattern[128];
    const char* p;
    size_t pos;

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    p = strstr(json, pattern);
    if (!p)
    {
        return -1;
    }

    p += strlen(pattern);
    pos = (size_t)(p - json);
    JsonSkipWhitespace(json, &pos);
    if (json[pos] != ':')
    {
        return -1;
    }

    pos++;
    JsonSkipWhitespace(json, &pos);
    return JsonReadString(json, &pos, out, out_len);
}

int ParseToolCall(const char* assistant_response, ToolCall* tool_call)
{
    const char* json = FindJsonObject(assistant_response);
    if (!json)
    {
        return -1;
    }

    memset(tool_call, 0, sizeof(*tool_call));
    if (ExtractJsonStringValue(json, "name", tool_call->name, sizeof(tool_call->name)) != 0)
    {
        return -1;
    }

    if (strcmp(tool_call->name, "write_file") == 0)
    {
        if (ExtractJsonStringValue(json, "path", tool_call->path, sizeof(tool_call->path)) != 0) return -1;
        if (ExtractJsonStringValue(json, "content", tool_call->content, sizeof(tool_call->content)) != 0) return -1;
        return 0;
    }

    if (strcmp(tool_call->name, "read_file") == 0)
    {
        if (ExtractJsonStringValue(json, "path", tool_call->path, sizeof(tool_call->path)) != 0) return -1;
        return 0;
    }

    if (strcmp(tool_call->name, "run_cmd") == 0)
    {
        if (ExtractJsonStringValue(json, "command", tool_call->command, sizeof(tool_call->command)) != 0) return -1;
        return 0;
    }

    return -1;
}

int WriteFileTool(const char* path, const char* content, char* response, unsigned int response_len)
{
    char full_path[1024];
    FILE* f;

    if (BuildWorkspacePath(path, full_path, sizeof(full_path)) != 0)
    {
        snprintf(response, response_len, "Invalid path: %s", path);
        return -1;
    }

    f = fopen(full_path, "wb");
    if (!f)
    {
        snprintf(response, response_len, "write_file failed: %s", strerror(errno));
        return -1;
    }

    fwrite(content, 1, strlen(content), f);
    fclose(f);
    snprintf(response, response_len, "File written successfully: %s", path);
    return 0;
}

int ReadFileTool(const char* path, char* response, unsigned int response_len)
{
    char full_path[1024];
    FILE* f;
    size_t bytes_read;

    if (BuildWorkspacePath(path, full_path, sizeof(full_path)) != 0)
    {
        snprintf(response, response_len, "Invalid path: %s", path);
        return -1;
    }

    f = fopen(full_path, "rb");
    if (!f)
    {
        snprintf(response, response_len, "read_file failed: %s", strerror(errno));
        return -1;
    }

    bytes_read = fread(response, 1, response_len - 1, f);
    response[bytes_read] = '\0';
    fclose(f);
    return 0;
}

int RunCmdTool(const char* command, char* response, unsigned int response_len)
{
    char wrapped_command[2048];
    FILE* pipe;
    size_t total = 0;
    int status;
    int written = snprintf(wrapped_command, sizeof(wrapped_command), "cd %s && %s 2>&1", AGENT_WORKSPACE, command);

    if (written < 0 || (unsigned int)written >= sizeof(wrapped_command))
    {
        snprintf(response, response_len, "run_cmd failed: command is too long");
        return -1;
    }

    pipe = AGENT_POPEN(wrapped_command, "r");
    if (!pipe)
    {
        snprintf(response, response_len, "run_cmd failed: %s", strerror(errno));
        return -1;
    }

    while (total + 1 < response_len)
    {
        size_t n = fread(response + total, 1, response_len - total - 1, pipe);
        total += n;
        if (n == 0)
        {
            break;
        }
    }

    response[total] = '\0';
    status = AGENT_PCLOSE(pipe);

    if (response[0] == '\0')
    {
        snprintf(response, response_len, "Command completed with no output");
    }

    if (status != 0)
    {
        unsigned int len = (unsigned int)strlen(response);
        if (WIFEXITED(status))
        {
            snprintf(response + len, response_len - len, "\n[exit status: %d]", WEXITSTATUS(status));
        }
        else
        {
            snprintf(response + len, response_len - len, "\n[process ended abnormally]");
        }
        return -1;
    }

    return 0;
}

int ExecuteToolCall(const ToolCall* tool_call, char* response, unsigned int response_len)
{
    if (strcmp(tool_call->name, "write_file") == 0)
    {
        return WriteFileTool(tool_call->path, tool_call->content, response, response_len);
    }

    if (strcmp(tool_call->name, "read_file") == 0)
    {
        return ReadFileTool(tool_call->path, response, response_len);
    }

    if (strcmp(tool_call->name, "run_cmd") == 0)
    {
        return RunCmdTool(tool_call->command, response, response_len);
    }

    snprintf(response, response_len, "Unknown tool: %s", tool_call->name);
    return -1;
}


int main()
{
    int ret;
    char user_prompt[MAX_USER_PROMPT_LEN];

    AgentContext context;
    char* context_buffer = new char[MAX_CONTEXT_LEN];
    char llm_response[MAX_LLM_RESPONSE_LEN];
    char tool_response[MAX_TOOL_RESPONSE_LEN];
    ToolCall tool_call;

    ret = EnsureWorkspace();
    if (ret != 0)
    {
        printf("Failed to create workspace: %s\n", strerror(errno));
        goto Exit;
    }

    ret = BeginSession(&context, context_buffer, MAX_CONTEXT_LEN);
    if (ret != 0)
    {
        goto Exit;
    }

    while (true)
    {
        printf("\nuser> ");
        fflush(stdout);

        if (!fgets(user_prompt, sizeof(user_prompt), stdin))
        {
            break;
        }

        user_prompt[strcspn(user_prompt, "\r\n")] = '\0';
        if (user_prompt[0] == '\0')
        {
            continue;
        }

        ret = AppendUserPrompt(&context, user_prompt);
        if (ret != 0)
        {
            goto Exit;
        }

        while (true)
        {
            ret = CallLLM(context.text, llm_response, sizeof(llm_response));
            /* printf("\n--- DEBUG: Raw LLM Output ---\n%s\n--- END DEBUG ---\n", llm_response); */
            ret = AppendAssistantResponse(&context, llm_response);
            if (ret != 0)
            {
                goto Exit;
            }

            if (ParseToolCall(llm_response, &tool_call) != 0)
            {
                break;
            }

            printf("Calling tool: %s\n", tool_call.name);
            ExecuteToolCall(&tool_call, tool_response, sizeof(tool_response));
            printf("Tool response: %s\n", tool_response);

            ret = AppendToolResponse(&context, tool_response);
            if (ret != 0)
            {
                goto Exit;
            }
        }
    }

    ret = 0;
Exit:

    delete[] context_buffer;

    return ret;
}
